#include "data/pmtilesDataSource.h"

#include "platform.h"
#include "log.h"
#include "util/asyncWorker.h"
#include "util/zlibHelper.h"
#include "tile/tileTask.h"
#include "util/url.h"

#include "pmtiles/pmtiles.hpp"

#include <algorithm>
#include <cinttypes>
#include <condition_variable>
#include <cstring>
#include <fstream>

namespace Tangram {

// Maximum depth of PMTiles directory tree
static constexpr int MAX_DIRECTORY_DEPTH = 3;

// Heuristic multiplier for initial decompression buffer size
static constexpr int DECOMPRESSION_SIZE_MULTIPLIER = 4;

// Timeout for HTTP range requests (in seconds)
static constexpr int HTTP_REQUEST_TIMEOUT_SEC = 30;

PMTilesDataSource::PMTilesDataSource(Platform& _platform, const std::string& _path)
    : m_platform(_platform),
      m_path(_path),
      m_isHttp(false),
      m_rootDirCached(false) {
    
    // Determine if this is an HTTP or local file source
    m_isHttp = (_path.substr(0, 7) == "http://" || _path.substr(0, 8) == "https://");
    
    // Create async worker for I/O operations
    m_worker = std::make_unique<AsyncWorker>("PMTiles");
    
    LOGD("PMTilesDataSource created for: %s (HTTP: %d)", _path.c_str(), m_isHttp);
}

PMTilesDataSource::~PMTilesDataSource() {
    clear();
}

void PMTilesDataSource::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_header.reset();
    m_rootDirCache.clear();
    m_rootDirCached = false;
}

bool PMTilesDataSource::readRangeSync(uint64_t offset, uint32_t length, std::vector<char>& data) {
    data.clear();
    data.reserve(length);
    
    if (m_isHttp) {
        // For HTTP sources, use HTTP range requests
        // Create Range header: "bytes=start-end"
        HttpOptions options;
        std::string rangeHeader = "bytes=" + std::to_string(offset) + "-" + 
                                  std::to_string(offset + length - 1);
        options.addHeader("Range", rangeHeader);
        
        // Use condition variable to wait for async response
        std::mutex cvMutex;
        std::condition_variable cv;
        bool completed = false;
        bool success = false;
        
        auto callback = [&](UrlResponse&& response) {
            std::lock_guard<std::mutex> lock(cvMutex);
            
            if (response.error) {
                LOGE("PMTiles: HTTP range request failed: %s", response.error);
            } else if (!response.content.empty()) {
                data = std::move(response.content);
                success = true;
            }
            
            completed = true;
            cv.notify_one();
        };
        
        // Start the HTTP range request
        m_platform.startUrlRequest(Url(m_path), options, std::move(callback));
        
        // Wait for the request to complete (with timeout)
        std::unique_lock<std::mutex> lock(cvMutex);
        if (!cv.wait_for(lock, std::chrono::seconds(HTTP_REQUEST_TIMEOUT_SEC), 
                        [&completed] { return completed; })) {
            LOGE("PMTiles: HTTP range request timed out after %d seconds", HTTP_REQUEST_TIMEOUT_SEC);
            return false;
        }
        
        if (!success) {
            LOGE("PMTiles: Failed to read range [%" PRIu64 ", %" PRIu64 ") from HTTP: %s",
                 offset, offset + length, m_path.c_str());
            return false;
        }
        
        // Verify we got the expected amount of data
        if (data.size() != length) {
            LOGW("PMTiles: Expected %u bytes, got %zu bytes from HTTP range request",
                 length, data.size());
        }
        
        return true;
        
    } else {
        // For local files, use standard file I/O with seeking
        std::ifstream file(m_path, std::ios::binary);
        if (!file.is_open()) {
            LOGE("PMTiles: Failed to open file: %s", m_path.c_str());
            return false;
        }
        
        file.seekg(offset);
        if (!file.good()) {
            LOGE("PMTiles: Failed to seek to offset %" PRIu64 " in file: %s", offset, m_path.c_str());
            return false;
        }
        
        data.resize(length);
        file.read(data.data(), length);
        if (!file.good() && !file.eof()) {
            LOGE("PMTiles: Failed to read %u bytes at offset %" PRIu64 " from file: %s", 
                 length, offset, m_path.c_str());
            return false;
        }
        
        return true;
    }
}

bool PMTilesDataSource::loadHeader() {
    if (m_header) {
        return true; // Already loaded
    }
    
    std::vector<char> headerData;
    if (!readRangeSync(0, 127, headerData)) {
        LOGE("PMTiles: Failed to read header from %s", m_path.c_str());
        return false;
    }
    
    try {
        std::string headerStr(headerData.begin(), headerData.end());
        auto header = pmtiles::deserialize_header(headerStr);
        m_header = std::make_unique<pmtiles::headerv3>(std::move(header));
        
        LOGD("PMTiles header loaded: zoom %d-%d, tiles: %llu, compression: %d, tile_type: %d",
             m_header->min_zoom, m_header->max_zoom, m_header->tile_entries_count,
             m_header->tile_compression, m_header->tile_type);
        
        return true;
    } catch (const std::exception& e) {
        LOGE("PMTiles: Failed to parse header: %s", e.what());
        return false;
    }
}

bool PMTilesDataSource::decompress(const std::vector<char>& compressed, 
                                   std::vector<char>& decompressed, 
                                   uint8_t compression) {
    switch (compression) {
        case pmtiles::COMPRESSION_NONE:
            decompressed = compressed;
            return true;
            
        case pmtiles::COMPRESSION_GZIP: {
            // Use existing zlib helper
            decompressed.clear();
            // Initial capacity estimate (heuristic: decompressed is ~4x compressed size)
            decompressed.reserve(compressed.size() * DECOMPRESSION_SIZE_MULTIPLIER);
            
            if (zlib_inflate(compressed.data(), compressed.size(), decompressed) == 0) {
                return true;
            }
            
            LOGE("PMTiles: Failed to decompress gzip data");
            return false;
        }
        
        case pmtiles::COMPRESSION_BROTLI:
            LOGE("PMTiles: Brotli compression not supported");
            return false;
            
        case pmtiles::COMPRESSION_ZSTD:
            LOGE("PMTiles: Zstd compression not supported");
            return false;
            
        default:
            LOGE("PMTiles: Unknown compression type: %d", compression);
            return false;
    }
}

bool PMTilesDataSource::loadDirectory(uint64_t offset, uint32_t length, 
                                     std::vector<char>& data) {
    // Read compressed directory
    std::vector<char> compressed;
    if (!readRangeSync(offset, length, compressed)) {
        return false;
    }
    
    // Decompress if needed
    if (!m_header) {
        LOGE("PMTiles: Header not loaded before directory");
        return false;
    }
    
    return decompress(compressed, data, m_header->internal_compression);
}

bool PMTilesDataSource::getTileData(const TileID& _tileId, std::vector<char>& _data) {
    // Ensure header is loaded (lazy initialization)
    if (!loadHeader()) {
        return false;
    }
    
    // Convert tangram TileID (z, x, y) to PMTiles tile ID
    // PMTiles uses a Hilbert curve encoding for efficient spatial indexing
    uint64_t tileId;
    try {
        tileId = pmtiles::zxy_to_tileid(_tileId.z, _tileId.x, _tileId.y);
    } catch (const std::exception& e) {
        LOGE("PMTiles: Invalid tile coordinates: z=%d, x=%d, y=%d: %s",
             _tileId.z, _tileId.x, _tileId.y, e.what());
        return false;
    }
    
    // Load root directory if not cached
    // The root directory is cached to avoid repeated reads for common operations
    if (!m_rootDirCached) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_rootDirCached) {
            if (!loadDirectory(m_header->root_dir_offset, 
                             static_cast<uint32_t>(m_header->root_dir_bytes), 
                             m_rootDirCache)) {
                LOGE("PMTiles: Failed to load root directory");
                return false;
            }
            m_rootDirCached = true;
        }
    }
    
    // Search for tile in directory hierarchy
    // PMTiles uses a tree structure with up to 3 levels for large tile sets
    std::string currentDir(m_rootDirCache.begin(), m_rootDirCache.end());
    uint64_t dirOffset = m_header->root_dir_offset;
    uint32_t dirLength = static_cast<uint32_t>(m_header->root_dir_bytes);
    
    // Traverse directory tree (max depth defined by PMTiles spec)
    for (int depth = 0; depth <= MAX_DIRECTORY_DEPTH; depth++) {
        std::vector<pmtiles::entryv3> entries;
        try {
            entries = pmtiles::deserialize_directory(currentDir);
        } catch (const std::exception& e) {
            LOGE("PMTiles: Failed to deserialize directory at depth %d: %s", depth, e.what());
            return false;
        }
        
        // Binary search for the tile ID in the directory
        auto entry = pmtiles::find_tile(entries, tileId);
        
        if (entry.length == 0) {
            // Tile not found in this archive
            return false;
        }
        
        if (entry.run_length > 0) {
            // This is a leaf entry - read the actual tile data
            std::vector<char> compressed;
            uint64_t tileOffset = m_header->tile_data_offset + entry.offset;
            
            if (!readRangeSync(tileOffset, entry.length, compressed)) {
                LOGE("PMTiles: Failed to read tile data at offset %" PRIu64 ", length %u",
                     tileOffset, entry.length);
                return false;
            }
            
            // Decompress tile data (gzip, brotli, or uncompressed)
            if (!decompress(compressed, _data, m_header->tile_compression)) {
                return false;
            }
            
            return true;
        } else {
            // This is a directory entry - load the next level directory
            std::vector<char> nextDirData;
            uint64_t nextDirOffset = m_header->leaf_dirs_offset + entry.offset;
            
            if (!loadDirectory(nextDirOffset, entry.length, nextDirData)) {
                LOGE("PMTiles: Failed to load directory at depth %d", depth + 1);
                return false;
            }
            
            currentDir = std::string(nextDirData.begin(), nextDirData.end());
        }
    }
    
    LOGE("PMTiles: Directory tree depth exceeded");
    return false;
}

bool PMTilesDataSource::loadTileData(std::shared_ptr<TileTask> _task, TileTaskCb _cb) {
    // Queue async work to avoid blocking the main thread
    m_worker->enqueue([this, _task, _cb]() {
        auto& task = static_cast<BinaryTileTask&>(*_task);
        
        std::vector<char> data;
        if (getTileData(_task->tileId(), data)) {
            // Successfully loaded tile from PMTiles archive
            task.rawTileData = std::make_shared<std::vector<char>>(std::move(data));
            
            // Invoke callback to notify that data is ready
            if (_cb.func) {
                _cb.func(_task);
            }
            return true;
        }
        
        // Tile not found in PMTiles archive
        // Try next source in chain (e.g., network source for fallback)
        if (next) {
            return next->loadTileData(_task, _cb);
        }
        
        // No tile data available from any source
        // Note: Not calling task.cancel() here to allow for proper error handling upstream
        if (_cb.func) {
            _cb.func(_task);
        }
        return false;
    });
    
    return true;
}

}
