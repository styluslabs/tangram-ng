#include "scene/scene.h"

#include "data/tileSource.h"
#include "data/rasterSource.h"
#include "gl/framebuffer.h"
#include "gl/shaderProgram.h"
#include "labels/labelManager.h"
#include "marker/markerManager.h"
#include "scene/dataLayer.h"
#include "scene/importer.h"
#include "scene/light.h"
#include "scene/sceneLoader.h"
#include "scene/spriteAtlas.h"
#include "scene/stops.h"
#include "selection/featureSelection.h"
#include "selection/selectionQuery.h"
#include "style/material.h"
#include "style/debugStyle.h"
#include "style/debugTextStyle.h"
#include "style/textStyle.h"
#include "style/pointStyle.h"
#include "style/rasterStyle.h"
#include "style/style.h"
#include "text/fontContext.h"
#include "util/base64.h"
#include "util/util.h"
#include "util/elevationManager.h"
#include "util/skyManager.h"
#include "util/yamlUtil.h"
#include "js/JavaScript.h"
#include "log.h"
#include "scene.h"

#include <algorithm>

namespace Tangram {

static std::atomic<int32_t> s_serial;
int64_t Scene::frameCount = 0;

Scene::Scene(Platform& _platform,
             SceneOptions&& _options,
             std::function<void(Scene*)> _prefetchCallback,
             Scene* _oldScene) :
    id(s_serial++),
    m_platform(_platform),
    m_options(std::move(_options)),
    m_tilePrefetchCallback(_prefetchCallback),
    m_sourceContext(_platform, this) {

    m_prana = std::make_shared<ScenePrana>(this);
    m_tileWorker = std::make_unique<TileWorker>(_platform, m_options.numTileWorkers);
    m_tileManager = std::make_unique<TileManager>(_platform, *m_tileWorker, m_prana);
    m_markerManager = std::make_unique<MarkerManager>(*this,
        _oldScene && _options.preserveMarkers ? _oldScene->m_markerManager.get() : NULL);
}

ScenePrana::~ScenePrana() {
    if (!m_scene) { return; }  // allow null Scene for use w/ alternative lifecycle management
    std::unique_lock<std::mutex> lock(m_scene->m_pranaMutex);
    m_scene->m_pranaDestroyed = true;
    m_scene->m_pranaCond.notify_all();
}

Scene::~Scene() {
    LOGD("Enter ~Scene() %d", id);
    // Release m_prana and wait for destruction via m_pranaMutex to ensure no TileTask callbacks can run
    //  from DataSource threads, esp. network response threads which have lifetime of Platform, not Scene!
    // Previous approach of locking weak_ptr<TileSource> in TileTask callbacks did not protect from calling
    //  into destroyed TileManager or TileWorker and could result in DataSource being destroyed on worker
    //  thread, causing deadlock attempting to join() worker thread.
    // We use weak_ptr<ScenePrana> instead of weak_ptr<Scene> to ensure Scene destroyed on Map worker thread
    // See https://stackoverflow.com/questions/45507041/ to support TileTask w/o ScenePrana set
    m_prana.reset();

    cancelTasks();  // normally no-op since this is called on main thread in Map before ~Scene()
    m_tileWorker->stop();  // this waits for worker threads

    {
        std::unique_lock<std::mutex> lock(m_pranaMutex);
        m_pranaCond.wait(lock, [&]{ return m_pranaDestroyed; });
    }
    LOGD("Finish ~Scene() %d", id);
}

void Scene::cancelTasks() {
    if (m_state == State::canceled) { return; }
    auto state = m_state;
    m_state = State::canceled;

    if (state == State::loading) {
        /// Cancel loading Scene data
        if (m_importer) {
            LOGD("Cancel Importer tasks");
            m_importer->cancelLoading();
        }
    }

    if (state == State::pending_resources) {
        /// NB: Called from main thread - notify async loader thread.
        std::unique_lock<std::mutex> lock(m_taskMutex);
        m_taskCondition.notify_one();
    }

    // tried canceling all URL requests at the Platform level, but this interferes w/ offline map download,
    //  so we instead need to ensure each stage (Importer, Scene::load(), TileManager) cancels its URL
    //  requests so they don't delay loading of next Scene
    //std::unique_lock<std::mutex> sceneCancelLock(m_sceneLoadMutex);
    //if (state != State::initial) { m_platform.cancelUrlRequest(UrlRequestHandle(-1)); }

    /// Cancels all TileTasks
    if (m_tileManager) {
        LOGD("Cancel TileManager tasks");
        m_tileManager->clearTileSets(true);
    }

    if (m_platform.activeUrlRequests() > 0) {
        LOGW("%d pending downloads remaining after Scene cancellation", m_platform.activeUrlRequests());
        //int req = 0; m_platform.cancelUrlRequest(req);  -- breakpoint on this to help find offending task
    }
}

// draw rule name in sublayer with parent that sets explicit style is incorrectly counted as used
static void getActiveStyles(const SceneLayer& layer, std::set<std::string>& activeStyles)
{
    if (!layer.enabled()) { return; }
    for (const DrawRuleData& rule : layer.rules()) {
        std::string style = rule.name;
        for (const StyleParam& param : rule.parameters) {
            if (param.key == StyleParamKey::style) {
                style = param.value.get<std::string>();
            } else if (param.key == StyleParamKey::outline_style) {
                activeStyles.emplace(param.value.get<std::string>());
            }
        }
        activeStyles.emplace(style);
    }
    for (const SceneLayer& sublayer : layer.sublayers()) {
        getActiveStyles(sublayer, activeStyles);
    }
}

bool Scene::load() {

    LOGTOInit();
    LOGTO(">>>>>> loadScene >>>>>>");

    //std::unique_lock<std::mutex> sceneLoadLock(m_sceneLoadMutex);

    auto isCanceled = [&](Scene::State test){
        if (m_state == test) { return false; }
        LOG("Scene got Canceled: %d %d", m_state, test);
        m_errors.emplace_back(SceneError{{}, Error::no_valid_scene});
        return true;
    };

    if (isCanceled(State::initial)) { return false; }

    m_state = State::loading;

    /// Wait until all scene-yamls are available and merged.
    /// NB: Importer holds reference to zip archives for resource loading
    ///
    /// Importer is blocking until all imports are loaded
    m_importer = std::make_unique<Importer>();
    m_config = m_importer->loadSceneData(m_platform, m_options.url, m_options.yaml);
    LOGTO("<<< applyImports");

    if (isCanceled(State::loading)) { return false; }

    if (!m_config) {
        LOGE("Scene loading failed: No config!");
        m_errors.emplace_back(SceneError{{}, Error::no_valid_scene});
        return false;
    }

    auto result = SceneLoader::applyUpdates(m_config, m_options.updates);
    if (result.error != Error::none) {
        m_errors.push_back(result);
        LOGE("Applying SceneUpdates failed (error %d)", int(result.error));
        return false;
    }
    LOGTO("<<< applyUpdates");

#ifdef TANGRAM_DUMP_MERGED_SCENE
    logMsg(YAML::Dump(m_config).c_str());
#endif

    Importer::resolveSceneUrls(m_config, m_options.url);

    SceneLoader::applyGlobals(m_config, m_config);
    LOGTO("<<< applyGlobals");

    m_tileSources = SceneLoader::applySources(m_config, m_options, m_sourceContext);
    LOGTO("<<< applySources");

    SceneLoader::applyCameras(m_config, m_camera);
    LOGTO("<<< applyCameras");

    m_lights = SceneLoader::applyLights(m_config["lights"]);
    m_lightShaderBlocks = Light::assembleLights(m_lights);
    LOGTO("<<< applyLights");

    const YAML::Node& sceneNode = m_config["scene"];
    SceneLoader::applyScene(sceneNode, m_background, m_backgroundStops, m_animated);
    bool terrain3d = YamlUtil::getBoolOrDefault(sceneNode["terrain_3d"], false);
    std::string elevSrc = sceneNode["elevation_source"].as<std::string>("");
    LOGTO("<<< applyScene");

    m_tileManager->setTileSources(m_tileSources);

    /// Scene is ready to load tiles for initial view
    if (m_options.prefetchTiles && m_tilePrefetchCallback) {
        m_tilePrefetchCallback(this);
    }

    m_fontContext = std::make_unique<FontContext>(m_platform);
    m_fontContext->loadFonts(m_options.fallbackFonts.empty() ?
                             m_platform.systemFontFallbacksHandle() : m_options.fallbackFonts);
    LOGTO("<<< initFonts");

    SceneLoader::applyFonts(m_config["fonts"], m_fonts);
    runFontTasks();
    LOGTO("<<< applyFonts");

    SceneLoader::applyTextures(m_config["textures"], m_textures);
    runTextureTasks();
    LOGTO("<<< textures");

    m_styles = SceneLoader::applyStyles(m_config["styles"], m_textures,
                                        m_jsFunctions, m_stops, m_names);
    LOGTO("<<< applyStyles");

    m_layers = SceneLoader::applyLayers(m_config["layers"], m_jsFunctions, m_stops, m_names);
    LOGTO("<<< applyLayers");

    /// Remove unused styles
    std::set<std::string> activeStyles;
    for (auto& layer : m_layers) {
        getActiveStyles(layer, activeStyles);
    }
    for (auto it = m_styles.begin(); it != m_styles.end();) {
        if (activeStyles.count((*it)->getName())) {
            ++it;
        } else {
            LOG("Discarding unused style '%s'", (*it)->getName().c_str());
            it = m_styles.erase(it);
        }
    }

    if (m_options.debugStyles) {
        m_styles.emplace_back(new DebugTextStyle("debugtext", true));
        m_styles.emplace_back(new DebugStyle("debug"));
    }
    /// Styles that are opaque must be ordered first in the scene so that
    /// they are rendered 'under' styles that require blending
    std::sort(m_styles.begin(), m_styles.end(), Style::compare);

    /// Post style sorting set their respective IDs=>vector indices
    /// These indices are used for style geometry lookup in tiles
    for(uint32_t i = 0; i < m_styles.size(); i++) {
        m_styles[i]->setID(i);
        if (auto pointStyle = dynamic_cast<PointStyle*>(m_styles[i].get())) {
            pointStyle->setTextures(m_textures.textures);
            pointStyle->setFontContext(*m_fontContext);
        }
        if (auto textStyle = dynamic_cast<TextStyle*>(m_styles[i].get())) {
            textStyle->setFontContext(*m_fontContext);
        }
    }
    runTextureTasks();
    LOGTO("<<< sortStyles");

    auto terrainSrcIt = std::find_if(m_tileSources.begin(), m_tileSources.end(),
        [&](auto& src){ return src->isRaster() && src->name() == elevSrc; });
    auto terrainSrc = terrainSrcIt != m_tileSources.end() ?
         std::static_pointer_cast<RasterSource>(*terrainSrcIt) : nullptr;
    // setup 3D terrain if enabled
    if (terrain3d) {
        // choose first raster style
        auto terrainStyle = std::find_if(m_styles.begin(), m_styles.end(),
              [](auto& style){ return style->type() == StyleType::raster; });
        if (terrainSrc && terrainStyle != m_styles.end()) {
            m_elevationManager = std::make_unique<ElevationManager>(terrainSrc, **terrainStyle);
        } else {
            LOGE("Unable to find elevation source or raster style needed for 3D terrain!");
        }
    }
    // need to keep elevation data if 3D terrain or contour labels enabled
    if (m_elevationManager || (terrainSrc && terrainSrc->TileSource::generateGeometry())) {
        terrainSrc->m_keepTextureData = true;
    }
    LOGTO("<<< elevationManager");

    // won't be initialized until sky is visible
    m_skyManager = std::make_unique<SkyManager>();

    for (auto& style : m_styles) { style->build(*this); }
    if (m_elevationManager) { m_elevationManager->m_style->build(*this); }
    LOGTO("<<< buildStyles");

    if (isCanceled(State::loading)) { return false; }

#ifdef TANGRAM_NATIVE_STYLE_FNS
    m_nativeFns.reserve(m_jsFunctions.size());
    for (const std::string& js : m_jsFunctions) {
        m_nativeFns.push_back(userGetStyleFunction(*this, js));
    }
#endif

    /// Now we are only waiting for pending fonts and textures:
    /// Let's initialize the TileBuilders on TileWorker threads
    /// in the meantime.
    m_tileWorker->setScene(*this);

    m_featureSelection = std::make_unique<FeatureSelection>();
    m_labelManager = std::make_unique<LabelManager>();

    m_state = State::pending_resources;

    bool startTileWorker = m_options.prefetchTiles;
    while (true) {
        // NB: Capture completion of tasks until wait(lock)
        // Otherwise we can loose the notify. We cannot lock m_tasksMutex
        // in task callback unless we require startUrlRequest to always
        // callback async..
        uint32_t tasksActive = m_tasksActive;

        std::unique_lock<std::mutex> lock(m_taskMutex);

        /// Check if scene-loading was canceled
        if (m_state != State::pending_resources) { break; }

        /// Don't need to wait for textures when their size is known
        bool canBuildTiles = true;

        m_textures.tasks.remove_if([&](auto& task) {
           if (!task.done && task.texture->width() == 0) {
               canBuildTiles = false;
           }
           return task.done;
        });

        m_fonts.tasks.remove_if([&](auto& task) {
            if (!task.done) {
                canBuildTiles = false;
                return false;
            }
            if (task.response.error) {
                LOGE("Error retrieving font '%s' at %s: ",
                     task.ft.uri.c_str(), task.response.error);
                return true;
            }
            auto&& data = task.response.content;
            m_fontContext->addFont(task.ft, std::move(data));  //alfons::InputSource(std::move(data)));
            return true;
        });

        /// Ready to build tiles?
        if (startTileWorker && canBuildTiles && m_tilePrefetchCallback) {
            m_readyToBuildTiles = true;
            startTileWorker = false;
            m_tilePrefetchCallback(this);
        }

        /// All done?
        if (m_textures.tasks.empty() && m_fonts.tasks.empty()) {
            m_readyToBuildTiles = true;
            break;
        }

        if (m_tasksActive != tasksActive) {
            continue;
        }
        LOGTO("Waiting for fonts and textures");
        m_taskCondition.wait(lock);
    }

    /// We got everything needed from Importer
    m_importer.reset();

    if (isCanceled(State::pending_resources)) {
        /// Cancel pending texture resources
        if (!m_textures.tasks.empty()) {
            LOG("Cancel texture resource tasks");
            for (auto& task : m_textures.tasks) {
                if (task.requestHandle) {
                    m_platform.cancelUrlRequest(task.requestHandle);
                }
            }
        }
        /// Cancel pending font resources
        if (!m_fonts.tasks.empty()) {
            LOG("Cancel font resource tasks");
            for (auto& task : m_fonts.tasks) {
                if (task.requestHandle) {
                    m_platform.cancelUrlRequest(task.requestHandle);
                }
            }
        }
        return false;
    }

    if (m_state == State::pending_resources) {
        m_state = State::pending_completion;
    }

    LOGTO("<<<<<< loadScene <<<<<<");
    return true;
}

void Scene::prefetchTiles(const View& _view) {
    View view = _view;

    view.setCamera(m_camera);

    if (m_options.useScenePosition) {
        view.setZoom(m_camera.startPosition.z);
        view.setPosition(m_camera.startPosition.x, m_camera.startPosition.y);
    }

    LOGTO(">>> loadTiles");
    LOG("Prefetch tiles for View: %fx%f / zoom:%f",  //lon:%f lat:%f",
        view.getWidth(), view.getHeight(), view.getZoom());
        //view.getCenterCoordinates().longitude, view.getCenterCoordinates().latitude);

    view.update();
    m_tileManager->updateTileSets(view);

    if (m_readyToBuildTiles) {
        m_pixelScale = _view.pixelScale();
        for (auto& style : m_styles) {
            style->setPixelScale(m_pixelScale);
        }
        m_fontContext->setPixelScale(m_pixelScale);

        m_tileWorker->startJobs();
    }
    LOGTO("<<< loadTiles");
}

bool Scene::completeScene(View& _view) {
    if (m_state == State::ready) { return true; }
    if (m_state != State::pending_completion) { return false; }

    _view.m_elevationManager = m_elevationManager.get();
    _view.setCamera(m_camera);

    if (m_options.useScenePosition) {
        _view.setZoom(m_camera.startPosition.z);
        _view.setPosition(m_camera.startPosition.x, m_camera.startPosition.y);
    }

    m_pixelScale = _view.pixelScale();
    m_fontContext->setPixelScale(m_pixelScale);

    for (auto& style : m_styles) {
        style->setPixelScale(m_pixelScale);
    }

    bool animated = m_animated == Scene::animate::yes;
    if (animated != m_platform.isContinuousRendering()) {
        m_platform.setContinuousRendering(animated);
    }

    m_state = State::ready;

    /// Tell TileWorker that Scene is ready, so it can check its work-queue
    m_tileWorker->startJobs();

    return true;
}

void Scene::setPixelScale(float _scale) {
    if (m_pixelScale == _scale) { return; }
    m_pixelScale = _scale;

    if (m_state != State::ready) {
        /// We update styles pixel scale in 'complete()'.
        /// No need to clear TileSets at this point.
        return;
    }

    for (auto& style : m_styles) {
        style->setPixelScale(_scale);
    }
    m_fontContext->setPixelScale(_scale);

    /// Tiles must be rebuilt to apply the new pixel scale to labels.
    m_tileManager->clearTileSets();

    /// Markers must be rebuilt to apply the new pixel scale.
    m_markerManager->rebuildAll();
}

std::shared_ptr<Texture> SceneTextures::add(const std::string& _name, const Url& _url,
                                            const TextureOptions& _options) {

    std::shared_ptr<Texture> texture = std::make_shared<Texture>(_options);
    textures.emplace(_name, texture);

    if (_url.hasBase64Data() && _url.mediaType() == "image/png") {
        auto data = _url.data();
        std::vector<unsigned char> blob;
        try {
            blob = Base64::decode(data);
        } catch(const std::runtime_error& e) {
            LOGE("Can't decode Base64 texture '%s'", e.what());
        }

        if (blob.empty()) {
            LOGE("Can't decode Base64 texture");

        } else if (!texture->loadImageFromMemory(blob.data(), blob.size())) {
            LOGE("Invalid Base64 texture");
        }
        return texture;
    } else if (_url.hasData() && _url.mediaType().substr(0,13) == "image/svg+xml") {
#ifdef TANGRAM_SVG_LOADER
        if (!userLoadSvg(_url.data().c_str(), _url.data().size(), texture.get())) {
            LOGE("Error parsing svg for texture '%s'", _name.c_str());
        }
#else
        LOGE("SVG support not enabled - cannot load texture '%s'", _name.c_str());
#endif
        return texture;
    }

    tasks.emplace_front(_url, texture);

    return texture;
}

std::shared_ptr<Texture> SceneTextures::add(const std::string& _name, int _width, int _height,
                                            const uint8_t* _data, const TextureOptions& _options) {
    std::shared_ptr<Texture> texture = std::make_shared<Texture>(_options);
    textures.emplace(_name, texture);
    int bpp = _options.bytesPerPixel();
    texture->setPixelData(_width, _height, _options.bytesPerPixel(), _data, bpp*_width*_height);
    return texture;
}

std::shared_ptr<Texture> SceneTextures::get(const std::string& _name) {
    auto entry = textures.find(_name);
    if (entry != textures.end()) {
        return entry->second;
    }
    /// If texture could not be found by name then interpret name as URL
    TextureOptions options;
    return add(_name, Url(_name), options);
}

void Scene::runTextureTasks() {

    std::weak_ptr<ScenePrana> wprana(m_prana);
    for (auto& task : m_textures.tasks) {
        if (task.started) { continue; }
        task.started = true;

        LOG("Fetch texture %s", task.url.string().c_str());

        auto cb = [this, wprana, &task](UrlResponse&& response) {
            auto sprana = wprana.lock();  // protect against running callback after scene destruction
            if(!sprana || m_state == State::canceled) { return; }
            LOG("Received texture %s", task.url.string().c_str());
            if (response.error) {
                LOGE("Error retrieving URL '%s': %s", task.url.string().c_str(), response.error);
            } else {
                /// Decode texture on download thread.
                auto& texture = task.texture;
                if (Url::getPathExtension(task.url.string()) == "svg") {
#ifdef TANGRAM_SVG_LOADER
                    if (!userLoadSvg(response.content.data(), response.content.size(), texture.get())) {
                        LOGE("Error loading texture data from URL '%s'", task.url.string().c_str());
                    }
#else
                    LOGE("SVG support not enabled - cannot load '%s'", task.url.string().c_str());
#endif
                } else {
                    auto data = reinterpret_cast<const uint8_t*>(response.content.data());
                    if (!texture->loadImageFromMemory(data, response.content.size())) {
                        LOGE("Invalid texture data from URL '%s'", task.url.string().c_str());
                    }
                }
                if (auto& sprites = texture->spriteAtlas()) {
                    sprites->updateSpriteNodes({texture->width(), texture->height()});
                }
            }

            std::unique_lock<std::mutex> lock(m_taskMutex);
            task.done = true;
            m_tasksActive--;
            m_taskCondition.notify_one();
        };

        m_tasksActive++;
        if (task.url.scheme() == "zip") {
            m_importer->readFromZip(task.url, std::move(cb));
        } else {
            task.requestHandle = m_platform.startUrlRequest(task.url, std::move(cb));
        }
    }
}

void SceneFonts::add(const std::string& _uri, const std::string& _family,
                     const std::string& _style, const std::string& _weight) {

    std::string familyNormalized, styleNormalized;
    familyNormalized.resize(_family.size());
    styleNormalized.resize(_style.size());

    std::transform(_family.begin(), _family.end(), familyNormalized.begin(), ::tolower);
    std::transform(_style.begin(), _style.end(), styleNormalized.begin(), ::tolower);
    auto desc = FontDescription{ familyNormalized, styleNormalized, _weight, _uri};

    tasks.emplace_front(Url(_uri), desc);
}

void Scene::runFontTasks() {

    std::weak_ptr<ScenePrana> wprana(m_prana);
    for (auto& task : m_fonts.tasks) {
        if (task.started) { continue; }
        task.started = true;

        LOG("Fetch font %s", task.ft.uri.c_str());

        auto cb = [this, wprana, &task](UrlResponse&& response) {
            auto sprana = wprana.lock();  // protect against running callback after scene destruction
            if(!sprana || m_state == State::canceled) { return; }

            std::unique_lock<std::mutex> lock(m_taskMutex);
            LOG("Received font: %s", task.ft.uri.c_str());
            task.response = std::move(response);
            task.done = true;

            m_tasksActive--;
            m_taskCondition.notify_one();
        };

        m_tasksActive++;
        if (task.url.scheme() == "zip") {
            m_importer->readFromZip(task.url, std::move(cb));
        } else {
            task.requestHandle = m_platform.startUrlRequest(task.url, std::move(cb));
        }
    }
}

Scene::UpdateState Scene::update(RenderState& _rs, View& _view, float _dt) {

    m_time += _dt;

    bool viewChanged = _view.update();

    auto markersState = m_markerManager->update(_view, _dt);

    bool tilesChanged = m_tileManager->updateTileSets(_view);

    for (const auto& style : m_styles) {
        style->onBeginUpdate();
    }

    auto& tiles = m_tileManager->getVisibleTiles();
    auto& markers = m_markerManager->markers();

    bool changed = viewChanged || tilesChanged || markersState.dirty;
    if (changed) {
        for (const auto& tile : tiles) {
            tile->update(_view, _dt);
        }
    }

    // because of 1 frame lag for terrain depth, we must always render even if onlyRender = true for
    //  updateLabelSet() since label coordinates will still be updated
    if (m_elevationManager) {
        m_elevationManager->renderTerrainDepth(_rs, _view, tiles);
    }

    m_labelManager->updateLabelSet(_view, _dt, *this, tiles, markers, !changed);

    return { m_tileManager->numLoadingTiles() > 0, m_labelManager->needUpdate(), markersState.easing };
}

void Scene::renderBeginFrame(RenderState& _rs) {
    _rs.setFrameTime(m_time);
    ++frameCount;

    for (const auto& style : m_styles) {
        style->onBeginFrame(_rs);
    }
}

bool Scene::render(RenderState& _rs, View& _view) {

    bool drawnAnimatedStyle = false;

    // draw the sky (if horizon if visible)
    m_skyManager->draw(_rs, _view);

    for (const auto& style : m_styles) {

        bool styleDrawn = style->draw(_rs, _view,
                                      m_tileManager->getVisibleTiles(),
                                      m_markerManager->markers());

        drawnAnimatedStyle |= (styleDrawn && style->isAnimated());
    }
    return drawnAnimatedStyle;
}

void Scene::renderSelection(RenderState& _rs, View& _view, FrameBuffer& _selectionBuffer,
                            std::vector<SelectionQuery>& _selectionQueries) {

    GLuint selectionVAO = 0;
    if(Hardware::supportsVAOs) {  // bind VAO in case hardware requires it (GL 3)
        GL::genVertexArrays(1, &selectionVAO);
        GL::bindVertexArray(selectionVAO);
    }

    for (const auto& style : m_styles) {

        style->drawSelectionFrame(_rs, _view,
                                  m_tileManager->getVisibleTiles(),
                                  m_markerManager->markers());
    }

    if(selectionVAO) { GL::deleteVertexArrays(1, &selectionVAO); }

    std::vector<SelectionColorRead> colorCache;
    /// Resolve feature selection queries
    for (const auto& selectionQuery : _selectionQueries) {
        selectionQuery.process(_view, _selectionBuffer,
                               *m_markerManager, *m_tileManager,
                               *m_labelManager, colorCache);
    }
}

std::shared_ptr<Texture> Scene::getTexture(const std::string& textureName) const {
    auto texIt = m_textures.textures.find(textureName);
    if (texIt == m_textures.textures.end()) {
        return nullptr;
    }
    return texIt->second;
}

std::shared_ptr<TileSource> Scene::getTileSource(int32_t id) const {
    auto it = std::find_if(m_tileSources.begin(), m_tileSources.end(),
                           [&](auto& s){ return s->id() == id; });
    if (it != m_tileSources.end()) {
        return *it;
    }
    return nullptr;
}

Color Scene::backgroundColor(int _zoom) const {
    if (m_backgroundStops.frames.size() > 0) {
        return m_backgroundStops.evalColor(_zoom);
    }
    return m_background;
}

JSFunctionIndex DataSourceContext::createFunction(const std::string& source) {
    std::unique_lock<std::mutex> lock(m_jsMutex);
    if(!m_jsContext) { m_jsContext = std::make_unique<JSContext>(); }
    m_jsContext->setFunction(m_functionIndex, source);
    return m_functionIndex++;
}

DataSourceContext::JSLockedContext DataSourceContext::getJSContext() {
    std::unique_lock<std::mutex> lock(m_jsMutex);
    if(!m_jsContext) {
        m_jsContext = std::make_unique<JSContext>();
        if(!m_scene) {
            JSScope scope(*m_jsContext);
            m_jsContext->setGlobalValue("global", YamlUtil::toJSValue(scope, m_globals));
        }
    }

    if(m_scene && globalsGeneration < m_scene->globalsGeneration) {
        globalsGeneration = m_scene->globalsGeneration;
        JSScope scope(*m_jsContext);
        m_jsContext->setGlobalValue("global", YamlUtil::toJSValue(scope, m_scene->config()["global"]));
    }

    return {std::move(lock), m_jsContext.get()};  //JSLockedScope(*m_jsContext, std::move(lock));
}

DataSourceContext::DataSourceContext(Platform& _platform, Scene* _scene)
    : m_globals(_scene->config()["globals"]), m_platform(_platform), m_scene(_scene) {}

DataSourceContext::DataSourceContext(Platform& _platform, const YAML::Node& _globals)
    : m_globals(_globals), m_platform(_platform), m_scene(nullptr) {}

DataSourceContext::~DataSourceContext() {}

}
