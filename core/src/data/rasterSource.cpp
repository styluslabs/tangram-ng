#include "data/rasterSource.h"
#include "data/propertyItem.h"
#include "data/tileData.h"
#include "tile/tile.h"
#include "tile/tileBuilder.h"
#include "tile/tileTask.h"
#include "util/mapProjection.h"
#include "log.h"

namespace Tangram {

class RasterTileTask : public BinaryTileTask {
public:

    const bool subTask = false;

    std::unique_ptr<Texture> texture;
    std::unique_ptr<Raster> raster;

    RasterTileTask(const TileID& _tileId, TileSource* _source, bool _subTask)
        : BinaryTileTask(_tileId, _source),
          subTask(_subTask) {}


    RasterSource* rasterSource() {
        return static_cast<RasterSource*>(source());  //std::static_pointer_cast<RasterSource>(m_source.lock());
    }

    bool hasData() const override {
        // probably should be "return BinaryTileTask::hasData() || ..."
        return bool(rawTileData) || bool(texture) || bool(raster);
    }

    void process(TileBuilder& _tileBuilder) override {
        auto source = rasterSource();
        assert(!m_ready);  // shared task previously could be erroneously added to tile worker queue twice

        if (!texture && !raster) {
            // Decode texture data
            texture = source->createTexture(m_tileId, *rawTileData);
            if (!texture) {
                // cancel on decode failure to match behavior of TileTask (and behavior for download failure)
                //  empty texture will be set in addRaster() if no proxy available
                //raster = std::make_unique<Raster>(m_tileId, source->emptyTexture());
                cancel();
                return;
            }
        }

        // Create tile geometries
        if (!subTask) {
          // make raster available for tile builder; RasterSource texture cache is not thread-safe currently
          //  so we can't add raster for good until complete() on main thread; note the empty deleter since
          //  shared_ptr doesn't have a release() method
          auto ptex = raster ? raster->texture : std::shared_ptr<Texture>(texture.get(), [](auto* t){});
          m_tile = std::make_unique<Tile>(m_tileId, source->id(), source->generation());
          m_tile->rasters().emplace_back(m_tileId, ptex);
          _tileBuilder.build(*m_tile, *(source->m_tileData), *source);
          m_tile->rasters().pop_back();
        }
        m_ready = true;
    }

    void addRaster(Tile& _tile) {
        auto source = rasterSource();
        if (!raster) {
          auto tex = source->cacheTexture(m_tileId, std::move(texture));
          raster = std::make_unique<Raster>(m_tileId, tex);
        }
        _tile.rasters().emplace_back(raster->tileID, raster->texture);
    }

    void complete() override {

        addRaster(*m_tile);

        for (auto& subtask : m_subTasks) {
            //assert(subTask->isReady());
            subtask->complete(*this);
        }
    }

    void complete(TileTask& _mainTask) override {
        if (!isReady()) {  //isCanceled()?
            // check for alternative raster is now in TileManager
            auto source = rasterSource();
            _mainTask.tile()->rasters().emplace_back(tileId(), source->emptyTexture());
        } else {
            addRaster(*_mainTask.tile());
        }
    }
};


RasterSource::RasterSource(const std::string& _name, std::unique_ptr<DataSource> _sources,
                           TextureOptions _options, TileSource::ZoomOptions _zoomOptions)
    : TileSource(_name, std::move(_sources), _zoomOptions),
      m_texOptions(_options) {

    m_textures = std::make_shared<Cache>();
    m_emptyTexture = std::make_shared<Texture>(m_texOptions);

    GLubyte pixel[4] = { 0, 0, 0, 0 };
    auto bpp = _options.bytesPerPixel();
    m_emptyTexture->setPixelData(1, 1, bpp, pixel, bpp);
}

void RasterSource::generateGeometry(bool _generateGeometry) {
    m_generateGeometry = _generateGeometry;

    if (m_generateGeometry) {
        Feature rasterFeature;
        rasterFeature.geometryType = GeometryType::polygons;
        rasterFeature.polygons = { { {
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {1.0f, 1.0f},
                    {0.0f, 1.0f},
                    {0.0f, 0.0f}
                } } };
        rasterFeature.props = Properties();

        m_tileData = std::make_shared<TileData>();
        m_tileData->layers.emplace_back("");
        m_tileData->layers.back().features.push_back(rasterFeature);
    }
}

std::unique_ptr<Texture> RasterSource::createTexture(TileID _tile, const std::vector<char>& _rawTileData) {
    if (_rawTileData.empty()) { return nullptr; }

    auto data = reinterpret_cast<const uint8_t*>(_rawTileData.data());
    auto length = _rawTileData.size();
    auto tex = std::make_unique<Texture>(m_texOptions, !m_keepTextureData);
    if (!tex->loadImageFromMemory(data, length)) { tex.reset(); }
    return tex;
}

std::shared_ptr<TileData> RasterSource::parse(const TileTask& _task) const {
    assert(false);
    return nullptr;
}

TileID RasterSource::subTaskTileID(const TileSource& _src, TileID _tile) const {
    // apply apt downsampling for raster tiles depending on difference
    // in zoomBias (which also takes zoom offset into account)
    auto zoomDiff = m_zoomOptions.zoomBias - _src.zoomBias();
    if (zoomDiff > 0) { _tile = _tile.zoomBiasAdjusted(zoomDiff); }
    return _tile.withMaxSourceZoom(m_zoomOptions.maxZoom);
}

void RasterSource::addRasterTask(TileTask& _task) {
    auto rasterTask = createRasterTask(subTaskTileID(*_task.source(), _task.tileId()), true);

    _task.subTasks().push_back(rasterTask);
}

std::shared_ptr<RasterTileTask> RasterSource::createRasterTask(TileID _tileId, bool subTask) {
    auto task = std::make_shared<RasterTileTask>(_tileId, this, subTask);

    // First try existing textures cache
    TileID id(_tileId.x, _tileId.y, _tileId.z);

    auto texIt = m_textures->find(id);
    if (texIt != m_textures->end()) {
        auto texture = texIt->second.lock();

        if (texture) {
            LOGV("%d - reuse %s", m_textures->size(), id.toString().c_str());

            task->raster = std::make_unique<Raster>(id, texture);
            // No more loading needed.
            task->startedLoading();
            if (subTask) { task->setReady(); }
        }
    }
    return task;
}

std::shared_ptr<TileTask> RasterSource::createTask(TileID _tileId) {
    auto task = createRasterTask(_tileId, false);

    addRasterTasks(*task);

    return task;
}

std::shared_ptr<Texture> RasterSource::cacheTexture(const TileID& _tileId, std::unique_ptr<Texture> _texture) {
    assert(_texture && _texture->bufferSize() > 0 && _texture.get() != m_emptyTexture.get());
    TileID id(_tileId.x, _tileId.y, _tileId.z);

    auto& textureEntry = (*m_textures)[id];
    auto texture = textureEntry.lock();
    if (texture) {
        LOGV("%d - drop duplicate %s", m_textures->size(), id.toString().c_str());
        // The same texture has been loaded in the meantime: Reuse it and drop _texture..
        return texture;
    }

    texture = std::shared_ptr<Texture>(_texture.release(),
                                       [c = std::weak_ptr<Cache>(m_textures), id](auto* t) {
                                           if (auto cache = c.lock()) {
                                               cache->erase(id);
                                               LOGV("%d - remove %s", cache->size(), id.toString().c_str());
                                           }
                                           delete t;
                                       });
    // Add to cache
    textureEntry = texture;
    LOGV("%d - added %s", m_textures->size(), id.toString().c_str());

    return texture;
}

std::shared_ptr<Texture> RasterSource::getTexture(TileID _tile) {
    auto texIt = m_textures->find(_tile);
    return texIt != m_textures->end() ? texIt->second.lock() : nullptr;
}

Raster RasterSource::getRaster(ProjectedMeters _meters) {
    if (m_textures->empty()) { return Raster(NOT_A_TILE, nullptr); }

    TileID tileId = MapProjection::projectedMetersTile(_meters, m_textures->begin()->first.z);
    auto minz = m_textures->rbegin()->first.z;
    do {
        auto tex = getTexture(tileId);
        if(tex) { return Raster(tileId, tex); }
        tileId = tileId.getParent();
    } while(tileId.z >= minz);

    return Raster(NOT_A_TILE, nullptr);
}

}
