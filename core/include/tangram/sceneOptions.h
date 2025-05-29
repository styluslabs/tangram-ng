#pragma once

#include "util/url.h"
#include "platform.h"

#include <functional>
#include <string>
#include <vector>


namespace Tangram {

class Scene;

struct SceneUpdate {
    std::string path;
    std::string value;
    SceneUpdate(std::string p, std::string v) : path(p), value(v) {}
    SceneUpdate() {}
};


class SceneOptions {
public:
    explicit SceneOptions(const Url& _url, bool _useScenePosition = false,
                          const std::vector<SceneUpdate>& _updates = {})
        : url(_url), updates(_updates),
          useScenePosition(_useScenePosition) {}

    explicit SceneOptions(const std::string& _yaml, const Url& _resources,
                          bool _useScenePosition = false,
                          const std::vector<SceneUpdate>& _updates = {})
        : yaml(_yaml), url(_resources), updates(_updates),
          useScenePosition(_useScenePosition) {}

    SceneOptions() : numTileWorkers(0) {}

    /// Scene as yaml string
    std::string yaml;

    /// URL from which this scene is loaded, or resourceRoot
    Url url;

    /// SceneUpdates to apply to the scene
    std::vector<SceneUpdate> updates;

    /// Set the view to the position provided by the scene
    bool useScenePosition = true;

    /// Add styles toggled by DebugFlags
    bool debugStyles = false;

    /// Start loading tiles as soon as possible
    bool prefetchTiles = true;

    /// Preserve markers from previous scene?
    bool preserveMarkers = false;

    /// Metric or Imperial?
    bool metricUnits = true;

    /// Number of threads fetching tiles
    uint32_t numTileWorkers = 2;

    /// 16MB default in-memory DataSource cache
    size_t memoryTileCacheSize = CACHE_SIZE;

    /// persistent MBTiles DataSource cache
    size_t diskTileCacheSize = 0;

    /// default max-age (in seconds) for disk tile cache
    int64_t diskTileCacheMaxAge = 180*24*60*60;  // 180 days in seconds

    /// cache directory for tiles, fonts, etc
    std::string diskCacheDir;

    /// global fallback fonts
    std::vector<FontSourceHandle> fallbackFonts;

private:
    static constexpr size_t CACHE_SIZE = 16 * (1024 * 1024);

};

}
