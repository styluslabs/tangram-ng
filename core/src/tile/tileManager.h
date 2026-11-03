#pragma once

#include "data/tileData.h"
#include "data/tileSource.h"
#include "tile/tile.h"
#include "tile/tileID.h"
#include "tile/tileTask.h"
#include "tile/tileWorker.h"

#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <set>
#include <vector>

namespace Tangram {

class Platform;
class TileSource;
class TileCache;
class View;
struct ViewState;

/* Singleton container of <TileSet>s
 *
 * TileManager is a singleton that maintains a set of Tiles based on the current
 * view into the map
 */
class TileManager {

    const static size_t DEFAULT_CACHE_SIZE = 32*1024*1024; // 32 MB

public:

    TileManager(Platform& platform, TileTaskQueue& _tileWorker, std::weak_ptr<ScenePrana> _prana);

    virtual ~TileManager();

    /* Sets the tile TileSources */
    void setTileSources(const std::vector<std::shared_ptr<TileSource>>& _sources);

    /* Updates visible tile set and load missing tiles */
    bool updateTileSets(const View& _view);

    void clearTileSets(bool clearSourceCaches = false);

    void clearTileSet(int32_t _sourceId);

    /* Returns the set of currently visible tiles */
    const auto& getVisibleTiles() const { return m_tiles; }

    int numLoadingTiles() const { return m_tilesInProgress; }
    int numTotalTiles() const;

    /* True if any dispatched tile or raster task is still being fetched or built off the main thread.
     * Main thread only; used by the benchmark replay to wait for loading without running frames. */
    bool tasksInFlight() const;

    std::shared_ptr<TileSource> getTileSource(int32_t _sourceId);

    void addClientTileSource(std::shared_ptr<TileSource> _source);

    bool removeClientTileSource(int32_t _sourceId);

    const std::unique_ptr<TileCache>& getTileCache() const { return m_tileCache; }

    /* @_cacheSize: Set size of in-memory tile cache in bytes.
     * This cache holds recently used <Tile>s that are ready for rendering.
     */
    void setCacheSize(size_t _cacheSize);

protected:

    enum class ProxyID : uint8_t;
    struct TileEntry;

    struct TileSet {
        TileSet(std::shared_ptr<TileSource> _source);
        ~TileSet();
        //void cancelTasks();

        std::shared_ptr<TileSource> source;

        std::set<TileID> visibleTiles;
        std::map<TileID, TileEntry> tiles;

        int64_t sourceGeneration = 0;

        TileSet(const TileSet&) = delete;
        TileSet(TileSet&&) = default;
        TileSet& operator=(const TileSet&) = delete;
        TileSet& operator=(TileSet&&) = default;
    };

    void updateTileSet(TileSet& tileSet, const ViewState& _view);

    void enqueueTask(TileSet& _tileSet, const TileID& _tileID, const ViewState& _view);

    void loadTiles();

    // add new visible tile to TileSet
    bool addTile(TileSet& _tileSet, const TileID& _tileID);

    // check cache for proxy for new tile
    void updateProxyTiles(TileSet& _tileSet, const TileID& _tileID, TileEntry& _tile);

    TileSet* findTileSet(int64_t sourceId);

    int32_t m_tilesInProgress = 0;

    std::vector<TileSet> m_tileSets;
    std::vector<TileSet> m_auxTileSets;

    /* Current tiles ready for rendering */
    std::vector<std::shared_ptr<Tile>> m_tiles;

    std::unique_ptr<TileCache> m_tileCache;
    size_t m_maxCacheLimit = DEFAULT_CACHE_SIZE;

    TileTaskQueue& m_workers;

    std::weak_ptr<ScenePrana> m_scenePrana;

    bool m_tileSetChanged = false;

    /* Callback for TileSource:
     * Passes TileTask back with data for further processing by <TileWorker>s
     */
    TileTaskCb m_dataCallback;

    /* Temporary list of tiles that need to be loaded */
    struct TileLoadTask { double dist; TileSet* tileSet; TileID tileID; };
    std::vector<TileLoadTask> m_loadTasks;

};

}
