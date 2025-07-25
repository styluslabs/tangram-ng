#pragma once

#include "map.h"
#include "platform.h"
#include "stops.h"
#include "sceneOptions.h"
#include "styleContext.h"
#include "util/fontDescription.h"
#include "tile/tileManager.h"
#include "util/color.h"
#include "util/url.h"
#include "util/yamlPath.h"
#include "view/view.h"

#include <atomic>
#include <forward_list>
#include <functional>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <list>

#include "glm/vec2.hpp"
#include "gaml/src/yaml.h"

namespace Tangram {

class DataLayer;
class FeatureSelection;
class FontContext;
class FrameBuffer;
class Importer;
class LabelManager;
class Light;
class MapProjection;
class MarkerManager;
class Platform;
class SceneLayer;
class SelectionQuery;
class Style;
class Texture;
class TileSource;
class ElevationManager;
class SkyManager;
struct SceneLoader;

struct SceneCamera : public Camera {
    glm::dvec3 startPosition;
};

using SceneFunctions = std::vector<std::string>;

using SceneStops = std::list<Stops>;

using DrawRuleNames = std::vector<std::string>;

struct SceneTextures {
    struct Task {
        Task(Url url, std::shared_ptr<Texture> texture) : url(url), texture(texture) {}
        bool started = false;
        bool done = false;
        Url url;
        std::shared_ptr<Texture> texture;
        UrlRequestHandle requestHandle = 0;
    };

    std::unordered_map<std::string, std::shared_ptr<Texture>> textures;

    std::forward_list<Task> tasks;

    std::shared_ptr<Texture> add(const std::string& name, const Url& url,
                                 const TextureOptions& options);
    std::shared_ptr<Texture> add(const std::string& _name, int _width, int _height,
                                 const uint8_t* _data, const TextureOptions& _options);

    std::shared_ptr<Texture> get(const std::string& name);
};

struct SceneFonts {
    struct Task {
        Task(Url url, FontDescription ft) : url(url), ft(ft) {}
        bool started = false;
        bool done = false;
        Url url;
        FontDescription ft;
        UrlRequestHandle requestHandle = 0;
        UrlResponse response;
    };
    std::forward_list<Task> tasks;

    void add(const std::string& _uri, const std::string& _family,
             const std::string& _style, const std::string& _weight);
};

class DataSourceContext {
    std::unique_ptr<JSContext> m_jsContext;
    std::mutex m_jsMutex;
    int64_t globalsGeneration = -1;
    JSFunctionIndex m_functionIndex = 0;
    const YAML::Node& m_globals;

    Platform& m_platform;
    Scene* m_scene;

public:
    DataSourceContext(Platform& _platform, Scene* _scene);
    DataSourceContext(Platform& _platform, const YAML::Node& _globals);
    ~DataSourceContext();

    struct JSLockedContext { std::unique_lock<std::mutex> lock; JSContext* ctx; };

    JSFunctionIndex createFunction(const std::string& source);
    std::unique_lock<std::mutex> getJSLock() { return std::unique_lock<std::mutex>(m_jsMutex); }
    JSLockedContext getJSContext();
    Platform& getPlatform() const { return m_platform; }
};

class ScenePrana {
public:
    ScenePrana(Scene* _scene) : m_scene(_scene) {}
    ~ScenePrana();

    Scene* m_scene;
};

// TODO: define in makefile, not here!
#define TANGRAM_SVG_LOADER 1
#ifdef TANGRAM_SVG_LOADER
  bool userLoadSvg(const char* svg, size_t len, Texture* texture);
#endif

class Scene {
public:
    enum animate { yes, no, none };

    Scene(Platform& _platform, SceneOptions&& = {},
          std::function<void(Scene*)> _prefetchCallback = nullptr, Scene* _oldScene = nullptr);

    ~Scene();

    Scene(const Scene& _other) = delete;
    Scene(Scene&& _other) = delete;

    /// Load the whole Scene
    bool load();

    auto& tileSources() const { return m_tileSources; }
    auto& featureSelection() const { return m_featureSelection; }
    auto& fontContext() const { return m_fontContext; }
    // so we can call SceneTextures::add() ... should we use a Scene::addTexture() instead?
    auto& sceneTextures() { return m_textures; }

    const auto& config() const { return m_config; }
    const auto& functions() const { return m_jsFunctions; }
    const auto& layers() const { return m_layers; }
    const auto& lightBlocks() const { return m_lightShaderBlocks; }
    const auto& lights() const { return m_lights; }
    const auto& options() const { return m_options; }
    const auto& styles() const { return m_styles; }
    const auto& textures() const { return m_textures.textures; }

    const auto& nativeFns() const { return m_nativeFns; }
    auto& nativeContext() { return m_nativeContext; }

    std::shared_ptr<TileSource> getTileSource(int32_t id) const;
    std::shared_ptr<Texture> getTexture(const std::string& name) const;

    std::shared_ptr<ScenePrana> prana() { return m_prana; }

    animate animated() const { return m_animated; }

    float pixelScale() const { return m_pixelScale; }
    void setPixelScale(float _scale);

    /// Update TileManager, Labels and Markers for current View
    struct UpdateState {
        bool tilesLoading, animateLabels, animateMarkers;
    };
    UpdateState update(RenderState& _rs, View& _view, float _dt);

    void renderBeginFrame(RenderState& _rs);
    bool render(RenderState& _rs, View& _view);
    void renderSelection(RenderState& _rs, View& _view,
                         FrameBuffer& _selectionBuffer,
                         std::vector<SelectionQuery>& _selectionQueries);

    Color backgroundColor(int _zoom) const;

    /// Used for FrameInfo debug
    TileManager* tileManager() const { return m_tileManager.get(); }
    LabelManager* labelManager() const { return m_labelManager.get(); }
    MarkerManager* markerManager() const { return m_markerManager.get(); }
    ElevationManager* elevationManager() const { return m_elevationManager.get(); }

    const SceneError* errors() const {
        return (m_errors.empty() ? nullptr : &m_errors.front());
    }

    /// When Scene is async loading this function can be run from
    /// main-thread to start loading tiles for the current view.
    /// - Copy current View width,height,pixelscale and position
    ///   (unless options.useScenePosition is set)
    /// - Start tile-loading for this View.
    void prefetchTiles(const View& view);

    /// Returns true when scene-loading could be completed,
    /// false when resources for tile-building and rendering
    /// are still pending.
    /// Does the finishing touch when everything is available:
    /// - Sets Scene camera to View
    /// - Update Styles and FontContext to current pixelScale
    /// - Sets platform continuousRendering mode
    bool completeScene(View& view);

    /// Cancel scene loading and all TileManager tasks
    void cancelTasks();

    /// Returns true when scene finished loading and completeScene() suceeded.
    bool isReady() const { return m_state == State::ready; }
    bool isPendingCompletion() const { return m_state == State::pending_completion; }

    /// Scene ID
    const int32_t id;

    /// incremented whenever globals are updated
    int64_t globalsGeneration = 0;

    /// set to hide labels with transition.selected < 0
    bool hideExtraLabels = false;

    /// global frame count
    static int64_t frameCount;

    using Lights = std::vector<std::unique_ptr<Light>>;
    using LightShaderBlocks = std::map<std::string, std::string>;
    using TileSources = std::vector<std::shared_ptr<TileSource>>;
    using Styles = std::vector<std::unique_ptr<Style>>;
    using Layers = std::vector<DataLayer>;

protected:

    Platform& m_platform;

    SceneOptions m_options;
    std::function<void(Scene*)> m_tilePrefetchCallback;

    std::unique_ptr<Importer> m_importer;

    /// Only SceneUpdate errors for now
    std::vector<SceneError> m_errors;

    enum class State {
        initial,
        loading,             // set at start of Scene::load()
        pending_resources,   // set while waiting for (async) resource loading tasks
        pending_completion,  // set end of Scene::load()
        ready,               // set on main thread when Scene::complete() succeeded
        canceled,            // should stop any scene- or tile-loading tasks
    };

    State m_state = State::initial;

    /// ---------------------------------------------------------------///
    /// Loaded Scene Data
    /// The root node of the YAML scene configuration
    YAML::Node m_config;

    /// syncronization for TileTasks
    friend class ScenePrana;
    std::mutex m_pranaMutex;
    std::condition_variable m_pranaCond;
    bool m_pranaDestroyed = false;
    std::shared_ptr<ScenePrana> m_prana;

    /// object to manage JS context used for (optional) tile URL fns - must be destroyed after TileSources
    DataSourceContext m_sourceContext;

    SceneCamera m_camera;

    Layers m_layers;
    TileSources m_tileSources;
    Styles m_styles;

    Lights m_lights;
    LightShaderBlocks m_lightShaderBlocks;

    void runTextureTasks();
    SceneTextures m_textures;

    void runFontTasks();
    SceneFonts m_fonts;

    /// Container of all strings used in styling rules; these need to be
    /// copied and compared frequently when applying styling, so rules use
    /// integer indices into this container to represent strings
    DrawRuleNames m_names;

    SceneFunctions m_jsFunctions;
    SceneStops m_stops;

    Color m_background;
    Stops m_backgroundStops;
    animate m_animated = none;

#ifdef TANGRAM_NATIVE_STYLE_FNS
    std::shared_ptr<void> m_nativeContext;
    NativeStyleFns m_nativeFns;
#endif

    /// ---------------------------------------------------------------///
    /// Runtime Data
    float m_pixelScale = 1.0f;
    float m_time = 0.0;

    /// Set true when all resources for TileBuilder are available
    bool m_readyToBuildTiles = false;

    std::unique_ptr<FontContext> m_fontContext;
    std::unique_ptr<FeatureSelection> m_featureSelection;
    std::unique_ptr<TileWorker> m_tileWorker;
    std::unique_ptr<TileManager> m_tileManager;
    std::unique_ptr<MarkerManager> m_markerManager;
    std::unique_ptr<LabelManager> m_labelManager;
    std::unique_ptr<ElevationManager> m_elevationManager;
    std::unique_ptr<SkyManager> m_skyManager;

    std::mutex m_taskMutex;
    std::atomic_uint m_tasksActive{0};
    std::condition_variable m_taskCondition;
};

}
