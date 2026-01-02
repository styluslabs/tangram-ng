#include "data/mbtilesDataSource.h"

#include "util/asyncWorker.h"
#include "util/zlibHelper.h"
#include "log.h"
#include "platform.h"
#include "util/url.h"
#include "util/util.h"

#define SQLITEPP_LOGW LOGW
#define SQLITEPP_LOGE LOGE
#include "sqlitepp.h"
#include "hash-library/md5.cpp"


namespace Tangram {

/**
 * The schema.sql used to set up an MBTiles Database.
 *
 * https://github.com/mapbox/node-mbtiles/blob/4bbfaf991969ce01c31b95184c4f6d5485f717c3/lib/schema.sql
 */
static const char* SCHEMA = R"SQL_ESC(BEGIN;

CREATE TABLE IF NOT EXISTS map (
   zoom_level INTEGER,
   tile_column INTEGER,
   tile_row INTEGER,
   tile_id TEXT
   -- grid_id TEXT
);

CREATE TABLE IF NOT EXISTS images (
    tile_data BLOB,
    tile_id TEXT,
    created_at INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS metadata (
    name TEXT,
    value TEXT
);

CREATE TABLE IF NOT EXISTS offline_tiles (
    tile_id TEXT,
    offline_id INTEGER
);

CREATE TABLE IF NOT EXISTS tile_last_access (
    tile_id TEXT,
    last_access INTEGER
);

CREATE UNIQUE INDEX IF NOT EXISTS map_index ON map (zoom_level, tile_column, tile_row);
CREATE UNIQUE INDEX IF NOT EXISTS images_id ON images (tile_id);
CREATE UNIQUE INDEX IF NOT EXISTS name ON metadata (name);
CREATE UNIQUE INDEX IF NOT EXISTS offline_index ON offline_tiles (tile_id, offline_id);
CREATE UNIQUE INDEX IF NOT EXISTS last_access_index ON tile_last_access (tile_id);
-- need index on map.tile_id for tile deletion
CREATE INDEX IF NOT EXISTS map_tile_id ON map (tile_id);

-- or we could use foreign keys: "tile_id REFERENCES images.tile_id ON DELETE CASCADE"
CREATE TRIGGER IF NOT EXISTS delete_tile AFTER DELETE ON images
BEGIN
    DELETE FROM map WHERE tile_id = OLD.tile_id;
    DELETE FROM tile_last_access WHERE tile_id = OLD.tile_id;
    --DELETE FROM offline_tiles WHERE tile_id = OLD.tile_id;
END;

CREATE VIEW IF NOT EXISTS tiles AS
    SELECT
        map.zoom_level AS zoom_level,
        map.tile_column AS tile_column,
        map.tile_row AS tile_row,
        images.tile_data AS tile_data,
        images.tile_id AS tile_id
    FROM map
    JOIN images ON images.tile_id = map.tile_id;

PRAGMA user_version = 3;

COMMIT;)SQL_ESC";


struct MBTilesQueries {
    SQLiteStmt getTileData;  // SELECT statement from tiles view
    SQLiteStmt putMap = nullptr;  // REPLACE INTO statement in map table
    SQLiteStmt putImage = nullptr;  // REPLACE INTO statement in images table
    // caching and offline maps
    SQLiteStmt getOffline = nullptr;
    SQLiteStmt putOffline = nullptr;
    SQLiteStmt putLastAccess = nullptr;

    struct tag_cache {};
    MBTilesQueries(sqlite3* db);
    MBTilesQueries(sqlite3* db, tag_cache);
};

MBTilesQueries::MBTilesQueries(sqlite3* db) :
    getTileData(db, "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;") {}

MBTilesQueries::MBTilesQueries(sqlite3* db, tag_cache) :
    getTileData(db, "SELECT tile_data, images.tile_id, images.created_at FROM images JOIN map ON"
        " images.tile_id = map.tile_id WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;"),
    putMap(db, "REPLACE INTO map (zoom_level, tile_column, tile_row, tile_id) VALUES (?, ?, ?, ?);"),
    putImage(db, "REPLACE INTO images (tile_id, tile_data, created_at) VALUES (?, ?, CAST(strftime('%s') AS INTEGER));"),
    getOffline(db, "SELECT 1,tile_id FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;"),
    putOffline(db, "REPLACE INTO offline_tiles (tile_id, offline_id) VALUES (?, ?);"),
    putLastAccess(db, "REPLACE INTO tile_last_access (tile_id, last_access) VALUES"
        " (?, CAST(strftime('%s') AS INTEGER));") {}

MBTilesDataSource::MBTilesDataSource(Platform& _platform, std::string _name, std::string _path,
                                     std::string _mime, int64_t _maxCacheAge, bool _offlineFallback)
    : m_name(_name),
      m_path(_path),
      m_mime(_mime),
      m_cacheMode(_maxCacheAge > 0),
      m_maxCacheAge(_maxCacheAge),
      m_offlineMode(_offlineFallback),
      m_platform(_platform) {

    m_worker = std::make_unique<AsyncWorker>(("MBTilesDataSource worker: " + _name).c_str());
    // max_age > 2**30 is interpreted as minimum creation time (clamped to current time)
    if (m_maxCacheAge > (1<<30)) { m_maxCacheAge = std::min(m_maxCacheAge, int64_t(secSinceEpoch())); }

    openMBTiles();
}

// need explicit destructor since MBTilesQueries is incomplete in header
MBTilesDataSource::~MBTilesDataSource() {}

bool MBTilesDataSource::loadTileData(std::shared_ptr<TileTask> _task, TileTaskCb _cb) {

    // currently, DataSource.level is always zero because SceneLoader doesn't use DataSource::setNext()
    if (m_offlineMode) {
        if (_task->rawSource == this->level) {
            // Try next source
            _task->rawSource = next->level;
        }

        return loadNextSource(_task, _cb);
    }

    if (!m_db) { return false; }

    if (_task->rawSource == this->level) {

        m_worker->enqueue([this, _task, _cb](){
            if (_task->isCanceled()) {  // task may have been canceled while in queue
              LOGV("%s - canceled tile: %s", m_name.c_str(), _task->tileId().toString().c_str());
              return;
            }
            auto prana = _task->prana();  // lock Scene when running callback on thread
            if (!prana) {
                LOGW("MBTilesDataSource callback for deleted Scene!");
                return;
            }
            TileID tileId = _task->tileId();
            LOGTO(">>> DB query for %s %s",
                  _task->source() ? _task->source()->name().c_str() : "?", tileId.toString().c_str());

            auto& task = static_cast<BinaryTileTask&>(*_task);
            auto tileData = std::make_unique<std::vector<char>>();
            // RasterTileTask::hasData() doesn't check if rawTileData is empty - it probably should, but
            //  let's not set rawTileData to empty vector, to match NetworkDataSource behavior
            int64_t createdAt = 0;
            getTileData(tileId, *tileData, createdAt, task.offlineId);
            LOGTO("<<< DB query for %s %s%s", _task->source() ? _task->source()->name().c_str() : "?",
                  tileId.toString().c_str(), tileData->empty() ? " (not found)" : "");

            // if tile is expired, request from network, falling back to stale tile on failure
            int64_t minCreatedAt = m_maxCacheAge > (1<<30) ? m_maxCacheAge
                                                           : int64_t(secSinceEpoch()) - m_maxCacheAge;
            TileTaskCb stalecb;
            if (next && m_cacheMode && createdAt < minCreatedAt) {
                LOGV("%s - stale tile: %s", m_name.c_str(), tileId.toString().c_str());
                // can't capture unique_ptr, and callback not guaranteed to be called so can't use bare ptr
                // ... and we don't want to put stale data in rawTileData or we'd need a flag to skip writing
                //  back to DB (erroneously updating creation time) should network request fail
                std::shared_ptr<std::vector<char>> staleData(std::move(tileData));
                stalecb.func = [_cb, staleData](std::shared_ptr<TileTask> _task2) {
                    auto prana2 = _task2->prana();  // lock Scene when running callback on thread
                    if (!prana2) { return; }

                    if (!_task2->hasData()) {
                        static_cast<BinaryTileTask&>(*_task2).rawTileData = staleData;
                    }
                    _cb.func(_task2);
                };
            }

            if (tileData && !tileData->empty()) {
                task.rawTileData = std::move(tileData);  // known data race w/ TileTask::hasData() on main thread
                LOGV("%s - loaded tile: %s, %d bytes", m_name.c_str(), tileId.toString().c_str(), task.rawTileData->size());

                _cb.func(_task);

            } else if (next) {
                LOGV("%s - requesting tile: %s", m_name.c_str(), tileId.toString().c_str());

                // Don't try this source again
                _task->rawSource = next->level;

                if (!loadNextSource(_task, stalecb.func ? stalecb :_cb)) {
                    // Trigger TileManager update so that tile will be
                    // downloaded next time.
                    _task->setNeedsLoading(true);
                    m_platform.requestRender();
                }
            } else {
                LOGD("%s - missing tile: %s", m_name.c_str(), _task->tileId().toString().c_str());
                _cb.func(_task);  // added 2022-09-27 ... were doing this in loadNextSource, why not here?
            }
        });
        return true;
    }

    return loadNextSource(_task, _cb);
}

bool MBTilesDataSource::loadNextSource(std::shared_ptr<TileTask> _task, TileTaskCb _cb) {
    if (!next) { return false; }

    if (!m_db) {
        return next->loadTileData(_task, _cb);
    }

    // Intercept TileTaskCb to store result from next source.
    TileTaskCb cb{[this, _cb](std::shared_ptr<TileTask> _task) {
        // it is expected that the DataSource `next` has called _task->prana() to lock the Scene
        //  if this callback is not run on the main thread
        if (_task->hasData()) {

            auto& task = static_cast<BinaryTileTask&>(*_task);
            std::shared_ptr<std::vector<char>> tileData = task.rawTileData;
            auto& zin = *task.rawTileData;
            if (zin.size() > 10 && zin[0] == 0x1F && (unsigned char)zin[1] == 0x8B) {
                auto pzout = std::make_shared<std::vector<char>>();
                if (zlib_inflate(zin.data(), zin.size(), *pzout) == 0) {
                    task.rawTileData = pzout;
                    // rawTileData now points to uncompressed data for building tile, while tileData points
                    //  to compressed data received from server to be stored in DB
                    if (m_cacheMode && m_schemaOptions.compression != Compression::undefined) {
                        m_db->exec("REPLACE INTO metadata (name, value) VALUES ('compression', 'undefined');");
                        m_schemaOptions.compression = Compression::undefined;
                    }
                }
            }

            if (m_cacheMode) {
                if (_task->offlineId) {
                    // for offline map download, we must force retry if storing tile fails (due to locked DB)
                    if (!storeTileData(task.tileId(), *tileData, task.offlineId)) {
                        task.rawTileData->clear();
                    }
                } else {
                    m_worker->enqueue([this, _task, tileData](){
                        storeTileData(_task->tileId(), *tileData);
                    });
                }
            }

            _cb.func(_task);

        } else if (m_offlineMode) {
            LOGD("try fallback tile: %s, %d", _task->tileId().toString().c_str());

            m_worker->enqueue([this, _task, _cb](){

                if (_task->isCanceled()) { return; }

                auto prana = _task->prana();  // lock Scene when running callback on thread
                if (!prana) { return; }

                auto& task = static_cast<BinaryTileTask&>(*_task);
                task.rawTileData = std::make_shared<std::vector<char>>();

                int64_t tileAge = 0;
                getTileData(_task->tileId(), *task.rawTileData, tileAge, task.offlineId);

                LOGV("loaded tile: %s, %d", _task->tileId().toString().c_str(), task.rawTileData->size());

                _cb.func(_task);

            });
        } else {
            LOGD("%s - missing tile: %s", m_name.c_str(), _task->tileId().toString().c_str());
            _cb.func(_task);
        }
    }};

    return next->loadTileData(_task, cb);
}

// when schema is modified, user_version should be incremented here and in SCHEMA block
static void runMigrations(SQLiteDB& db) {

    db.stmt("PRAGMA user_version;").exec([&](int ver){
        if(ver < 2) {
            // added column can only have a constant default value
            db.stmt("SELECT strftime('%s');").exec([&](std::string t){
                db.exec("ALTER TABLE images ADD COLUMN created_at INTEGER DEFAULT " + t + ";");
            });
        }
        if(ver < 3) {
            db.exec("CREATE INDEX IF NOT EXISTS map_tile_id ON map (tile_id);");
            db.exec("PRAGMA user_version = 3;");
        }
    });
}

void MBTilesDataSource::openMBTiles() {

    auto mode = SQLITE_OPEN_FULLMUTEX;
    mode |= m_cacheMode ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY;

    auto url = Url(m_path);
    auto path = url.path();
    const char* vfs = NULL;
    if (url.scheme() == "asset") {
        vfs = "ndk-asset";
        path.erase(path.begin()); // Remove leading '/'.
    }

    SQLiteDB db;
    if (sqlite3_open_v2(path.c_str(), &db.db, mode, vfs) != SQLITE_OK) {
        // ensure that we only run initSchema() on newly created file, not just if testSchema() fails (which
        //  can happen on valid existing database if locked)
        mode |= SQLITE_OPEN_CREATE;
        if (m_cacheMode && sqlite3_open_v2(path.c_str(), &db.db, mode, vfs) == SQLITE_OK) {
            LOG("Creating SQLite database %s", m_path.c_str());
            initSchema(db, m_name, m_mime);
        } else {
            LOGE("Unable to open SQLite database: %s - %s", m_path.c_str(), db.errMsg());
            return;
        }
    }
    LOG("SQLite database opened: %s", path.c_str());

    if (!testSchema(db)) {
        LOGE("Invalid MBTiles schema");
        return;
    }

    if (m_cacheMode && !m_schemaOptions.isCache) {
        LOGE("Cannot cache to externally created MBTiles database %s", m_path.c_str());
        m_cacheMode = false;  // should we allow reading from DB?
        return;
    }

    if (m_schemaOptions.compression == Compression::unsupported) {
        LOGE("MBTiles database has unsupported compression type: %s", m_path.c_str());
        return;
    }

    // schema updates
    if (m_cacheMode) {
        runMigrations(db);
    }

    m_queries = m_cacheMode ? std::make_unique<MBTilesQueries>(db.db, MBTilesQueries::tag_cache{})
        : std::make_unique<MBTilesQueries>(db.db);
    m_db = std::make_unique<SQLiteDB>(std::move(db));
}

/**
 * We check to see if the database has the MBTiles Schema.
 * Sets m_schemaOptions from metadata table
 *
 * @param _source A pointer to a the data source in which we will setup a db.
 * @return true if database contains MBTiles schema
 */
bool MBTilesDataSource::testSchema(SQLiteDB& db) {

    bool metadata = false, tiles = false;  //, grids = false, grid_data = false;

    // switching scenes rapidly can cause SQLITE_BUSY error when previous instance hasn't closed DB yet
    sqlite3_busy_timeout(db.db, 2000);

    db.stmt("SELECT name FROM sqlite_master WHERE type IN ('table', 'view')").exec([&](std::string name){
        // required
        if (name == "metadata") metadata = true;
        else if (name == "tiles") tiles = true;
        // optional
        //else if (name == "grids") grids = true;
        //else if (name == "grid_data") grid_data = true;
    });
    if (!metadata || !tiles) {
        LOGD("Missing MBTiles tables");
        return false;
    }

    db.stmt("SELECT value FROM metadata WHERE name = 'description';").exec([&](std::string desc){
        if (desc == "MBTiles tile container created by Tangram ES.") {
            m_schemaOptions.isCache = true;
        }
    });

    db.stmt("SELECT value FROM metadata WHERE name = 'compression';").exec([&](std::string cmpr){
        if (cmpr == "undefined" || cmpr == "unknown") {}
        else if (cmpr == "identity" || cmpr == "none") {
            m_schemaOptions.compression = Compression::identity;
        } else if (cmpr == "deflate" || cmpr == "gzip") {
            m_schemaOptions.compression = Compression::deflate;
        } else {
            LOGE("Unsupported MBTiles tile compression: %s", cmpr.c_str());
            m_schemaOptions.compression = Compression::unsupported;
        }
    });

    // one issue w/ WAL is lack of clean application exit on mobile to flush it
    //db.exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
    sqlite3_busy_timeout(db.db, 0);  // should not be any other writers

    return true;
}

void MBTilesDataSource::initSchema(SQLiteDB& db, std::string _name, std::string _mimeType) {

    // Otherwise, we need to execute schema.sql to set up the db with the right schema.
    db.exec(SCHEMA);
    // Fill in metadata table.
    // https://github.com/pnorman/mbtiles-spec/blob/2.0/2.0/spec.md#content
    // https://github.com/mapbox/mbtiles-spec/pull/46
    SQLiteStmt stmt = db.stmt("INSERT INTO metadata (name, value) VALUES (?, ?);");
    // name, type, version, description, format, compression
    stmt.bind("name", _name).exec();
    stmt.bind("type", "baselayer").exec();
    stmt.bind("version", "1").exec();
    stmt.bind("description", "MBTiles tile container created by Tangram ES.").exec();
    stmt.bind("format", _mimeType).exec();
    // Compression not yet implemented - no gain for raster tiles (png or jpg); gziping vector .mbtiles
    //  gave around 40% size reduction
    // http://www.iana.org/assignments/http-parameters/http-parameters.xhtml#content-coding
    // identity means no compression
    stmt.bind("compression", "identity").exec();
}

bool MBTilesDataSource::getTileData(const TileID& _tileId,
                                    std::vector<char>& _data, int64_t& _tileAge, int offlineId) {

    if (offlineId && !m_cacheMode) {
        LOGE("Offline tiles cannot be created: database is read-only!");
        return false;
    }
    // MBTiles uses TMS (tile row incr. south to north) while TileID is WMTS (tile row incr. north to south)
    int z = _tileId.z;
    int y = (1 << z) - 1 - _tileId.y;

    // offlineId > 0 indicates request to set offline_id; not necessary to read data
    if (offlineId > 0) {
        return m_queries->getOffline.bind(z, _tileId.x, y).exec([&](int, const char* tileid){
            if (m_queries->putOffline.bind(tileid, std::abs(offlineId)).exec()) {
                _data.push_back('\0');  // make TileTask::hasData() true if offline id written successfully
            }
        });
    }

    return m_queries->getTileData.bind(z, _tileId.x, y).exec([&](sqlite3_stmt* stmt){
        const char* blob = (const char*) sqlite3_column_blob(stmt, 0);
        const int length = sqlite3_column_bytes(stmt, 0);
        std::string tileid = m_cacheMode ? (const char*)sqlite3_column_text(stmt, 1) : "";
        _tileAge = m_cacheMode ? sqlite3_column_int64(stmt, 2) : 0;

        if ((m_schemaOptions.compression == Compression::undefined) ||
            (m_schemaOptions.compression == Compression::deflate)) {

            if (zlib_inflate(blob, length, _data) != 0) {
                if (m_schemaOptions.compression == Compression::undefined) {
                    _data.resize(length);
                    memcpy(_data.data(), blob, length);
                } else {
                    LOGW("Invalid deflate compression");
                }
            }
        } else {
            _data.resize(length);
            memcpy(_data.data(), blob, length);
        }
        if (offlineId) {
            if (!m_queries->putOffline.bind(tileid, std::abs(offlineId)).exec()) {
                _data.clear();  // force retry if writing offline id fails
            }
        }
        if (m_cacheMode) {
            m_queries->putLastAccess.bind(tileid).exec();
        }
    });
}

bool MBTilesDataSource::storeTileData(const TileID& _tileId, const std::vector<char>& _data, int offlineId) {
    int z = _tileId.z;
    int y = (1 << z) - 1 - _tileId.y;

    const char* data = _data.data();
    size_t size = _data.size();

    /**
     * We create an MD5 of the raw tile data. The MD5 functions as a hash
     * between the map and images tables. With this, tiles with duplicate
     * data will join to a single entry in the images table.
     */
    MD5 md5;
    std::string md5id = md5(data, size);

    do {
        if (!m_db->exec("BEGIN;")) { break; }
        if (!m_queries->putMap.bind(z, _tileId.x, y, md5id).exec()) { break; }
        sqlite3_bind_text(m_queries->putImage.stmt, 1, md5id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_blob(m_queries->putImage.stmt, 2, data, size, SQLITE_STATIC);
        if (!m_queries->putImage.exec()) { break; }

        if (offlineId) {
            if (!m_queries->putOffline.bind(md5id, std::abs(offlineId)).exec()) { break; }
        } else {
            if (!m_queries->putLastAccess.bind(md5id).exec()) { break; }
        }

        if (!m_db->exec("COMMIT;")) { break; }
        m_platform.notifyStorage(size, 0);
        LOGD("%s - store tile: %s (%d bytes)", m_name.c_str(), _tileId.toString().c_str(), int(size));
        return true;
    } while (0);

    LOGE("%s - SQL error storing tile %s: %s", m_name.c_str(), _tileId.toString().c_str(), m_db->errMsg());
    m_db->exec("ROLLBACK;");
    return false;
}

}
