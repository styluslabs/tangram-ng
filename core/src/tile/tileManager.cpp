#include "tile/tileManager.h"

#include "data/tileSource.h"
#include "data/rasterSource.h"
#include "map.h"
#include "platform.h"
#include "tile/tile.h"
#include "tile/tileCache.h"
#include "util/mapProjection.h"
#include "view/view.h"

#include "glm/gtx/norm.hpp"

#include <algorithm>
#include <bitset>

namespace Tangram {

#define MAX_TILE_SETS 64

struct TileManager::TileEntry {

    TileEntry(std::shared_ptr<Tile>& _tile) : tile(_tile) {}

    ~TileEntry() { clearTask(); }

    std::shared_ptr<Tile> tile;
    std::shared_ptr<TileTask> task;

    /* A Counter for number of tiles this tile acts a proxy for */
    int32_t m_proxyCounter = 0;

    // set if tile has failed raster subtasks
    int32_t numMissingRasters = -1;

    // is tile in TileSet.visibleTiles?
    bool m_visible = false;

    bool isInProgress() {
        return bool(task) && !task->isCanceled();
    }

    bool isCanceled() {
        return bool(task) && task->isCanceled();
    }

    bool needsLoading() {
        if (bool(tile)) { return false; }
        if (!task) { return true; }
        if (task->isCanceled()) { return false; }
        if (task->needsLoading()) { return true; }

        for (auto& subtask : task->subTasks()) {
            if (subtask->needsLoading() && !subtask->isCanceled()) { return true; }
        }
        return false;
    }

    // Complete task only when
    // - task still exists
    // - task has a tile ready
    // - tile has all rasters set
    bool completeTileTask() {
        if (bool(task) && task->isReady()) {

            for (auto& subtask : task->subTasks()) {
                if (!subtask->isReady() && !subtask->isCanceled()) { return false; }
            }

            task->complete();
            --task->shareCount;
            for (auto& subtask : task->subTasks()) {
              --subtask->shareCount;
            }
            tile = task->getTile();
            task.reset();
            numMissingRasters = -1;  // tile for ClientDataSource can be replaced w/o new TileEntry

            return true;
        }
        return false;
    }

    void clearTask() {
        if (!task) { return; }
        for (auto& subtask : task->subTasks()) {
            if (--subtask->shareCount <= 0 && !subtask->isCanceled()) {
                subtask->cancel();
                subtask->source()->cancelLoadingTile(*subtask);
            }
        }
        task->subTasks().clear();
        if (--task->shareCount <= 0 && !task->isCanceled()) {
            task->cancel();
            task->source()->cancelLoadingTile(*task);
        }
        task.reset();
    }

    // Is tile in TileSet.visibleTiles?
    bool isVisible() const {
        return m_visible;
    }

    void setVisible(bool _visible) {
        m_visible = _visible;
    }
};

TileManager::TileSet::TileSet(std::shared_ptr<TileSource> _source) : source(_source) {}

TileManager::TileSet::~TileSet() {}  //cancelTasks();

TileManager::TileManager(Platform& platform, TileTaskQueue& _tileWorker, std::weak_ptr<ScenePrana> _prana) :
    m_workers(_tileWorker), m_scenePrana(_prana) {

    m_tileCache = std::unique_ptr<TileCache>(new TileCache(DEFAULT_CACHE_SIZE));

    // Callback to pass task from Download-Thread to Worker-Queue
    m_dataCallback = TileTaskCb{[&](std::shared_ptr<TileTask> task) {

        if (task->isReady()) {
             platform.requestRender();

        } else if (task->hasData()) {
            m_workers.enqueue(task);

        } else {
            task->cancel();
        }
    }};
}

TileManager::~TileManager() {}  //m_tileSets.clear();

void TileManager::setTileSources(const std::vector<std::shared_ptr<TileSource>>& _sources) {

    m_tileCache->clear();
    assert(m_tileSets.empty() && m_auxTileSets.empty() && "setTileSources() should only be called once!");
    for (const auto& source : _sources) {
        if (source->generateGeometry()) {
            m_tileSets.emplace_back(source);
        } else {
            m_auxTileSets.emplace_back(source);
        }
    }
    while (m_tileSets.size() > MAX_TILE_SETS) { m_tileSets.pop_back(); }
}

std::shared_ptr<TileSource> TileManager::getTileSource(int32_t _sourceId) {
    auto it = std::find_if(m_tileSets.begin(), m_tileSets.end(),
         [&](auto& ts) { return ts.source->id() == _sourceId; });

    if (it != m_tileSets.end()) {
        return it->source;
    }
    return nullptr;
}

void TileManager::addClientTileSource(std::shared_ptr<TileSource> _tileSource) {
    auto it = std::find_if(m_tileSets.begin(), m_tileSets.end(),
        [&](auto& ts) { return ts.source->id() == _tileSource->id(); });

    if (it == m_tileSets.end()) {
        m_tileSets.emplace_back(_tileSource);
    }
}

bool TileManager::removeClientTileSource(int32_t _sourceId) {

    auto it = std::find_if(m_tileSets.begin(), m_tileSets.end(),
                           [&](auto& ts) { return ts.source->id() == _sourceId; });

    if (it != m_tileSets.end()) {
        m_tileSets.erase(it);
        return true;
    }
    return false;
}

void TileManager::clearTileSets(bool clearSourceCaches) {

    for (auto& tileSet : m_tileSets) {
        tileSet.tiles.clear();  //tileSet.cancelTasks();

        if (clearSourceCaches) {
            tileSet.source->clearData();
        }
    }

    m_tileCache->clear();
}

void TileManager::clearTileSet(int32_t _sourceId) {
    for (auto& tileSet : m_tileSets) {
        if (tileSet.source->id() != _sourceId) { continue; }

        tileSet.tiles.clear();  //tileSet.cancelTasks();
    }

    m_tileCache->clear();
    m_tileSetChanged = true;
}

bool TileManager::updateTileSets(const View& _view) {

    m_tiles.clear();
    m_tilesInProgress = 0;
    m_tileSetChanged = false;

    if (!getDebugFlag(DebugFlags::freeze_tiles)) {

        float maxEdge = 2 * _view.pixelScale() * float(MapProjection::tileSize());
        float maxArea = maxEdge*maxEdge;

        using TileSetMask = std::bitset<MAX_TILE_SETS>;
        // enable recursion by passing lambda ref to itself; auto type creates a generic (i.e. templated) lambda
        auto getVisibleTiles = [&](auto&& self, TileID tileId, TileSetMask active){
            // if pitch == 0, this will only return 0 or FLT_MAX
            float area = _view.getTileScreenArea(tileId);
            if (area <= 0) { return; }  // offscreen

            TileSetMask nextActive = active;
            for (size_t ii = 0; ii < m_tileSets.size(); ++ii) {
                if (!active[ii]) { continue; }
                auto& tileSet = m_tileSets[ii];
                int zoomBias = tileSet.source->zoomBias();
                // substantial redesign needed for something like this to work:
                //for (auto& rs : tileSet.source->rasterSources()) { maxZoom = std::max(maxZoom, rs->maxZoom()); }
                int maxZoom = std::min(tileSet.source->maxZoom(), _view.getIntegerZoom() - zoomBias);
                if (tileId.z >= maxZoom || area < maxArea*std::exp2(2*float(zoomBias))) {
                    TileID visId = tileId;
                    // Ensure that s = z + bias (larger s OK if overzoomed) so that proxy tiles can be found
                    // - otherwise, we get frames where tiles disappear due to no proxy for new tile
                    if (visId.z < tileSet.source->maxZoom()) {
                        visId.s = visId.z + zoomBias;
                    } else {
                        int s = tileId.z + std::max(0, int(std::ceil(std::log2(area/maxArea)/2)));
                        visId.s = std::max(std::min(s, _view.getIntegerZoom()), visId.z + zoomBias);
                    }
                    tileSet.visibleTiles.insert(visId);
                    nextActive.reset(ii);
                }
            }
            // subdivide if any active tile sets remaining
            if (nextActive.any()) {
                for (int i = 0; i < 4; i++) {
                    self(self, tileId.getChild(i, 100), nextActive);
                }
            }
        };

        for (auto& tileSet : m_tileSets) {
            tileSet.visibleTiles.clear();
        }

        TileSetMask allActive = (1 << m_tileSets.size()) - 1;
        getVisibleTiles(getVisibleTiles, TileID(0,0,0), allActive);
    }

    for (auto& tileSet : m_tileSets) {
        // check if tile set is active for zoom (zoom might be below min_zoom)
        if (tileSet.source->isActiveForZoom(_view.getZoom()) && tileSet.source->isVisible()) {
            updateTileSet(tileSet, _view.state());
        }
    }

    for (auto& tileSet : m_auxTileSets) {
        auto it = tileSet.tiles.begin();
        while (it != tileSet.tiles.end()) {
            if (!it->second.task || it->second.task->isReady() || it->second.task->isCanceled()) {
                it->second.task.reset();  // avoid call to clearTask() in ~TileEntry()
                it = tileSet.tiles.erase(it);
            } else {
                ++it;
            }
        }
    }

    loadTiles();

    // no longer need to sort or dedup m_tiles since it is populated in order from TileSet.tiles (std::map)

    // grow tile cache if needed - goal is to cache about 1 screen worth of tiles
    if (m_tileCache->cacheSizeLimit() < m_maxCacheLimit) {
        size_t memused = 0;
        for (const auto& tile : m_tiles) { memused += tile->getMemoryUsage(); }
        if (memused > 1.5*m_tileCache->cacheSizeLimit()) {
            m_tileCache->limitCacheSize(std::min(m_maxCacheLimit, memused));
        }
    }

    return m_tileSetChanged;
}

void TileManager::updateTileSet(TileSet& _tileSet, const ViewState& _view) {

    //FrameInfo::scope _trace("updateTileSet " + _tileSet.source->name());

    if (_tileSet.sourceGeneration != _tileSet.source->generation()) {
        _tileSet.sourceGeneration = _tileSet.source->generation();
    }

    auto& tiles = _tileSet.tiles;
    const auto& visibleTiles = _tileSet.visibleTiles;

    // Pending proxy tiles too far from current zoom level will be canceled
    int maxProxyZ = 0, minProxyZ = 0, maxVisS = 0;
    if (!visibleTiles.empty()) {
        int zmax = visibleTiles.begin()->z, zmin = visibleTiles.rbegin()->z;
        maxProxyZ = zmin != zmax ? zmax + 2 : int(glm::round(_view.zoom + 1.f));
        minProxyZ = zmin != zmax ? zmin - 3 : int(glm::round(_view.zoom - 2.f));
        maxVisS = visibleTiles.begin()->s;
    }

    // Loop over visibleTiles and add any needed tiles to tileSet
    auto curTilesIt = tiles.begin();
    auto visTilesIt = visibleTiles.begin();

    auto generation = _tileSet.source->generation();

    while (visTilesIt != visibleTiles.end() || curTilesIt != tiles.end()) {

        auto& visTileId = visTilesIt == visibleTiles.end() ? NOT_A_TILE : *visTilesIt;
        auto& curTileId = curTilesIt == tiles.end() ? NOT_A_TILE : curTilesIt->first;

        if (visTileId == curTileId) {
            // tiles in both sets match
            assert(visTilesIt != visibleTiles.end() &&
                   curTilesIt != tiles.end());

            auto& entry = curTilesIt->second;
            entry.setVisible(true);

            if (entry.completeTileTask()) {
                m_tileSetChanged = true;
            }

            if (entry.needsLoading()) {
                // Not yet available - enqueue for loading
                if (!entry.task) {
                    entry.task = _tileSet.source->createTask(visTileId);
                }
                enqueueTask(_tileSet, visTileId, _view);
            }

            // NB: Special handling to update tiles from ClientDataSource.
            // Can be removed once ClientDataSource is immutable
            if (entry.tile) {
                auto sourceGeneration = entry.tile->sourceGeneration();
                if ((sourceGeneration < generation) && !entry.isInProgress()) {
                    // Tile needs update - enqueue for loading
                    entry.task = _tileSet.source->createTask(visTileId);
                    enqueueTask(_tileSet, visTileId, _view);
                }
            } else if (entry.isCanceled()) {
                auto sourceGeneration = entry.task->sourceGeneration();
                if (sourceGeneration < generation) {
                    // Tile needs update - enqueue for loading
                    entry.task = _tileSet.source->createTask(visTileId);
                    enqueueTask(_tileSet, visTileId, _view);
                }
            }

            if (entry.isInProgress()) {
                m_tilesInProgress++;
            }

            ++curTilesIt;
            ++visTilesIt;

        } else if (curTileId > visTileId) {
            // tileSet is missing an element present in visibleTiles
            // NB: if (curTileId == NOT_A_TILE) it is always > visTileId
            //     and if curTileId > visTileId, then visTileId cannot be
            //     NOT_A_TILE. (for the current implementation of > operator)
            assert(visTilesIt != visibleTiles.end());

            if (!addTile(_tileSet, visTileId)) {
                // Not in cache - enqueue for loading
                enqueueTask(_tileSet, visTileId, _view);
                m_tilesInProgress++;
            }

            ++visTilesIt;

        } else {
            // tileSet has a tile not present in visibleTiles
            assert(curTilesIt != tiles.end());

            auto& entry = curTilesIt->second;
            if (entry.completeTileTask()) {
                m_tileSetChanged = true;
            }
            entry.setVisible(false);
            ++curTilesIt;
        }
    }

    int minCurS = tiles.rbegin()->first.s; //, maxCurS = tiles.begin()->first.s;
    auto zoomBias = _tileSet.source->zoomBias();
    // find proxy tiles
    for (curTilesIt = tiles.begin(); curTilesIt != tiles.end(); ++curTilesIt) {
        auto& tileId = curTilesIt->first;
        auto& entry = curTilesIt->second;
        if (!entry.isVisible()) {
            // handle child proxy (i.e. look for visible parents w/o tile)
            for (auto id = tileId.getParent(zoomBias); id.s >= minCurS; id = id.getParent(zoomBias)) {
                auto it = tiles.find(id);
                if (it == tiles.end()) { continue; }
                // visible tile w/ tile (so no proxy needed) or a better proxy found before visible tile?
                if (it->second.tile) { break; }
                // found visible tile (w/o tile) to proxy for?
                if (it->second.isVisible()) { entry.m_proxyCounter++; break; }
            }
        } else if (!entry.tile) {
            // visible tile w/o tile - look for parents which can be proxy
            for (auto id = tileId.getParent(zoomBias); id.s >= minCurS; id = id.getParent(zoomBias)) {
                auto it = tiles.find(id);
                if (it != tiles.end()) {
                    it->second.m_proxyCounter++;
                    if (it->second.tile) { break; }
                }
            }
        }
    }

    // add ready tiles to m_tiles and remove tiles not in visibleTiles and not being used as proxy
    for (curTilesIt = tiles.begin(); curTilesIt != tiles.end();) {
        auto& tileId = curTilesIt->first;
        auto& entry = curTilesIt->second;

#ifdef TANGRAM_DEBUG_TILESETS //0 && LOG_LEVEL >= 3
        size_t rasterLoading = 0, rasterDone = 0;
        if (entry.task) {
            for (auto &raster : entry.task->subTasks()) {
                if (raster->isReady()) { rasterDone++; }
                else { rasterLoading++; }
            }
        }
        LOGD("%s > %s - ready:%d proxy:%d/%d loading:%d rDone:%d rLoading:%d canceled:%d",
             _tileSet.source->name().c_str(), tileId.toString().c_str(), bool(entry.tile),
             entry.getProxyCounter(), entry.m_proxies, entry.task && !entry.task->isReady(),
             rasterDone, rasterLoading, entry.task && entry.task->isCanceled());
#endif

        bool canLoad = entry.isInProgress() && (tileId.z < maxProxyZ && tileId.z > minProxyZ);
        if (entry.isVisible() || (entry.m_proxyCounter > 0 && (entry.tile || canLoad))) {
            if (entry.tile) {
                entry.tile->setProxyDepth(entry.m_proxyCounter > 0 ? std::max(maxVisS - tileId.s, 1) : 0);
                m_tiles.push_back(entry.tile);
                // check to see if a replacement is now available for missing raster
                if (entry.numMissingRasters != 0) {
                    // to persist numMissingRasters for cached tiles, we'd need to store in Tile itself, so
                    //  instead we use a initial value of -1 to force check for new TileEntry, whether Tile
                    //  is new or from cache
                    auto& srcs = _tileSet.source->rasterSources();
                    auto& rasters = entry.tile->rasters();
                    size_t offset = rasters.size() - srcs.size();
                    entry.numMissingRasters = 0;
                    for (size_t ii = 0; ii < srcs.size(); ++ii) {
                        auto& raster = rasters[ii+offset];
                        TileID want = srcs[ii]->subTaskTileID(*_tileSet.source, tileId);
                        bool missing = raster.texture == srcs[ii]->emptyTexture();
                        if (!missing && raster.tileID.z >= want.z) { continue; }
                        // keep checking for exact raster while using proxy (e.g. if loaded by another tile set)
                        if (auto exact = srcs[ii]->getTexture(TileID(want.x, want.y, want.z))) {
                            raster.tileID = TileID(want.x, want.y, want.z, tileId.s);
                            raster.texture = exact;
                            continue;
                        }
                        ++entry.numMissingRasters;
                        if (!missing) { continue; }
                        TileID id(want.x, want.y, want.z);
                        do {
                            id = id.getParent();
                            auto proxy = srcs[ii]->getTexture(id);
                            if (proxy) {
                                raster.tileID = TileID(id.x, id.y, id.z, tileId.s);
                                raster.texture = proxy;
                                LOGD("Found proxy %s for missing '%s' subtask raster '%s' %s", id.toString().c_str(),
                                     _tileSet.source->name().c_str(), srcs[ii]->name().c_str(), tileId.toString().c_str());
                                break;
                            }
                        } while (id.z > 13 || (id.z > 0 && id.z + 2 >= tileId.z));
                    }
                }
            } else if (entry.isInProgress()) {
                auto& task = entry.task;
                // Update tile distance to map center for load priority.
                auto tileCenter = MapProjection::tileCenter(tileId);
                double scaleDiv = exp2(tileId.z - _view.zoom);
                if (scaleDiv < 1) { scaleDiv = 0.1/scaleDiv; } // prefer parent tiles
                task->setPriority(glm::length2(tileCenter - _view.center) * scaleDiv);
                task->setProxyState(entry.m_proxyCounter > 0);
            }
            entry.m_proxyCounter = 0;  // reset for next update
            ++curTilesIt;
        } else {
            // Remove entry and move tile (if present) to cache
            if (entry.tile) {
                m_tileCache->put(_tileSet.source->id(), entry.tile);
            }
            // Remove tile from set - this will call clearTask() and thus cancelLoadingTile() as appropriate
            curTilesIt = tiles.erase(curTilesIt);
        }
    }
}

void TileManager::enqueueTask(TileSet& _tileSet, const TileID& _tileID,
                              const ViewState& _view) {

    // Keep the items sorted by distance
    auto tileCenter = MapProjection::tileCenter(_tileID);
    double distance = glm::length2(tileCenter - _view.center);

    auto it = std::upper_bound(m_loadTasks.begin(), m_loadTasks.end(), distance,
        [](const double& d, const TileLoadTask& other){ return d < other.dist; });

    m_loadTasks.insert(it, {distance, &_tileSet, _tileID});
}

TileManager::TileSet* TileManager::findTileSet(int64_t sourceId) {
    for (auto& ts : m_tileSets) {
        if (ts.source->id() == sourceId) { return &ts; }
    }
    for (auto& ts : m_auxTileSets) {
        if (ts.source->id() == sourceId) { return &ts; }
    }
    return nullptr;
}

void TileManager::loadTiles() {

    if (m_loadTasks.empty()) { return; }

    for (auto& loadTask : m_loadTasks) {
        TileSet* tileSet = loadTask.tileSet;
        auto tileIt = tileSet->tiles.find(loadTask.tileID);
        auto tileTask = tileIt->second.task;

        for (auto& subtask : tileTask->subTasks()) {
            // needsLoading() will be false if, e.g., texture was already cached by RasterSource
            if (!subtask->needsLoading()) { ++subtask->shareCount; continue; }
            TileSet* ts = findTileSet(subtask->sourceId());
            if (!ts) { continue; }  // should never happen

            auto it = ts->tiles.find(subtask->tileId());
            if (it != ts->tiles.end()) {
                if (it->second.task && !it->second.task->isReady() && !it->second.task->isCanceled()) {
                    subtask = it->second.task;
                }
            } else if (!ts->source->generateGeometry()) {
                // add to aux tile set - this will be the master task for any subsequent duplicates
                std::shared_ptr<Tile> dummy;
                auto res = ts->tiles.emplace(subtask->tileId(), dummy);
                if (res.second) {
                    res.first->second.task = subtask;
                }
            }
            // shareCount > 1 prevents tile cancelation (shareCount decremented by cancel and complete)
            ++subtask->shareCount;
        }
        ++tileTask->shareCount;

        tileTask->setScenePrana(m_scenePrana);
        tileSet->source->loadTileData(tileTask, m_dataCallback);

        LOGTO("Load Tile: %s %s", tileSet->source->name().c_str(), loadTask.tileID.toString().c_str());
    }

    m_loadTasks.clear();
}

bool TileManager::addTile(TileSet& _tileSet, const TileID& _tileID) {

    auto tile = m_tileCache->get(_tileSet.source->id(), _tileID);

    if (tile) {
        if (tile->sourceGeneration() == _tileSet.source->generation()) {
            // Reset tile on potential internal dynamic data set
            tile->resetState();
        } else {
            // Clear stale tile data
            tile.reset();
        }
    }

    // Add TileEntry to TileSet
    auto entryit = _tileSet.tiles.emplace(_tileID, tile);
    TileEntry& entry = entryit.first->second;

    if (!tile) {
        // check cache for proxy (proxy already in TileSet will be found by updateTileSet())
        updateProxyTiles(_tileSet, _tileID, entry);

        entry.task = _tileSet.source->createTask(_tileID);
    }
    entry.setVisible(true);

    return bool(tile);
}

void TileManager::updateProxyTiles(TileSet& _tileSet, const TileID& _tileID, TileEntry& _tile) {
    // should we prefer child over parent as proxy?

    auto zoomBias = _tileSet.source->zoomBias();
    auto maxZoom = _tileSet.source->maxZoom();

    auto addProxy = [&](TileID id){
        auto tile = m_tileCache->get(_tileSet.source->id(), id);
        if (tile) { _tileSet.tiles.emplace(id, tile); }
        return bool(tile);
    };

    TileID parentId = _tileID.getParent(zoomBias);
    if (addProxy(parentId)) { return; }
    if (addProxy(parentId.getParent())) { return; }
    for(int ii = 0; ii < 4; ++ii) {
        addProxy(_tileID.getChild(ii, maxZoom));
    }
}

void TileManager::setCacheSize(size_t _cacheSize) {
    m_maxCacheLimit = _cacheSize;
    if (m_tileCache->cacheSizeLimit() > _cacheSize) {
        m_tileCache->limitCacheSize(_cacheSize);
    }
}

bool TileManager::tasksInFlight() const {
    // a task is in flight once loadTiles() has dispatched it (shareCount > 0) and data loading has
    //  started (!needsLoading()), until it is ready or canceled
    auto inFlight = [](const TileTask& t) {
        return !t.isCanceled() && !t.needsLoading() && !t.isReady();
    };
    for (auto* sets : {&m_tileSets, &m_auxTileSets}) {
        for (auto& ts : *sets) {
            for (auto& kv : ts.tiles) {
                auto& task = kv.second.task;
                if (!task || task->shareCount <= 0) { continue; }
                if (inFlight(*task)) { return true; }
                for (auto& subtask : task->subTasks()) {
                    if (inFlight(*subtask)) { return true; }
                }
            }
        }
    }
    return false;
}

int TileManager::numTotalTiles() const {
    int tot = 0;
    for (const auto& tileSet : m_tileSets) { tot += tileSet.visibleTiles.size(); }
    return tot;
}

}
