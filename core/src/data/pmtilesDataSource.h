#pragma once

#include "data/tileSource.h"
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace pmtiles {
    struct headerv3;
}

namespace Tangram {

class Platform;
class AsyncWorker;

/**
 * PMTilesDataSource provides tile data from PMTiles archives.
 * 
 * PMTiles is a cloud-optimized single-file archive format for tile pyramids,
 * designed as an alternative to MBTiles with better support for HTTP range requests.
 * 
 * Features:
 * - Supports both local files and HTTP sources
 * - Uses HTTP range requests for efficient cloud storage access
 * - No SQLite dependency - simple binary format with hierarchical directory
 * - Automatic decompression (gzip supported)
 * - Thread-safe async I/O operations
 * 
 * Usage in scene YAML:
 *   sources:
 *     my-tiles:
 *       type: MVT
 *       url: file:///path/to/data.pmtiles     # Local file
 *       # OR
 *       url: https://example.com/tiles.pmtiles # HTTP source
 */
class PMTilesDataSource : public TileSource::DataSource {
public:
    /**
     * Construct a PMTiles data source.
     * @param _platform Platform instance for file I/O and HTTP requests
     * @param _path Path to PMTiles file (local file path or HTTP URL)
     */
    PMTilesDataSource(Platform& _platform, const std::string& _path);
    
    ~PMTilesDataSource() override;

    /**
     * Load tile data asynchronously.
     * @param _task Tile task to load data for
     * @param _cb Callback to invoke when data is loaded
     * @return true if request was queued, false on immediate failure
     */
    bool loadTileData(std::shared_ptr<TileTask> _task, TileTaskCb _cb) override;

    /**
     * Clear cached data (header, directories)
     */
    void clear() override;

private:
    /**
     * Read a range of bytes from the PMTiles file synchronously.
     * For local files, uses standard file I/O.
     * For HTTP sources, uses HTTP range requests (blocks until complete).
     * @param offset Byte offset to start reading from
     * @param length Number of bytes to read
     * @param data Output buffer for the read data
     * @return true on success, false on failure
     */
    bool readRangeSync(uint64_t offset, uint32_t length, std::vector<char>& data);
    
    /**
     * Get tile data for a specific tile ID.
     * Traverses the PMTiles directory hierarchy to locate the tile.
     * @param _tileId Tile identifier (z, x, y)
     * @param _data Output buffer for tile data (decompressed)
     * @return true if tile found and loaded, false otherwise
     */
    bool getTileData(const TileID& _tileId, std::vector<char>& _data);
    
    /**
     * Decompress data based on compression type.
     * @param compressed Input compressed data
     * @param decompressed Output decompressed data
     * @param compression Compression type (COMPRESSION_NONE, COMPRESSION_GZIP, etc.)
     * @return true on success, false on failure
     */
    bool decompress(const std::vector<char>& compressed, std::vector<char>& decompressed, uint8_t compression);
    
    /**
     * Load and parse the PMTiles header (first 127 bytes).
     * Caches the header for subsequent calls.
     * @return true on success, false on failure
     */
    bool loadHeader();
    
    /**
     * Load and decompress a directory from the PMTiles file.
     * @param offset Byte offset of the directory
     * @param length Length of the compressed directory data
     * @param data Output buffer for decompressed directory data
     * @return true on success, false on failure
     */
    bool loadDirectory(uint64_t offset, uint32_t length, std::vector<char>& data);

    Platform& m_platform;
    std::string m_path;
    bool m_isHttp;  // true if path is HTTP/HTTPS URL
    
    // Cached header (loaded on first access, access should be serialized via loadHeader())
    // Note: Access to m_header should be done carefully in multi-threaded context
    std::unique_ptr<pmtiles::headerv3> m_header;
    
    // Worker thread for async I/O operations
    std::unique_ptr<AsyncWorker> m_worker;
    
    // Mutex for thread-safe access to cached data
    std::mutex m_mutex;
    
    // Cache for root directory (speeds up tile lookups)
    // Protected by m_mutex
    std::vector<char> m_rootDirCache;
    bool m_rootDirCached;
};

}
