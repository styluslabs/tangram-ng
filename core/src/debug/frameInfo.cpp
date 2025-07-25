#include "debug/frameInfo.h"

#include "debug/textDisplay.h"
#include "gl.h"
#include "gl/glError.h"
#include "gl/primitives.h"
#include "map.h"
#include "scene/scene.h"
#include "marker/markerManager.h"
#include "labels/labelManager.h"
#include "tile/tileCache.h"

#include <deque>
#include <ctime>
#if defined(DEBUG) && defined(TANGRAM_LINUX)
#include <malloc.h>
#endif

#define TIME_TO_MS(start, end) (float(end - start) / CLOCKS_PER_SEC * 1000.0f)

#define DEBUG_STATS_MAX_SIZE 128

namespace Tangram {

static float s_lastUpdateTime = 0.0;

static clock_t s_startFrameTime = 0,
    s_endFrameTime = 0,
    s_startUpdateTime = 0,
    s_endUpdateTime = 0;

void FrameInfo::beginUpdate() {

    if (getDebugFlag(DebugFlags::tangram_infos) || getDebugFlag(DebugFlags::tangram_stats)) {
        s_startUpdateTime = clock();
    }

}

void FrameInfo::endUpdate() {

    if (getDebugFlag(DebugFlags::tangram_infos) || getDebugFlag(DebugFlags::tangram_stats)) {
        s_endUpdateTime = clock();
        s_lastUpdateTime = TIME_TO_MS(s_startUpdateTime, s_endUpdateTime);
    }

}

void FrameInfo::beginFrame() {

    if (getDebugFlag(DebugFlags::tangram_infos) || getDebugFlag(DebugFlags::tangram_stats)) {
        s_startFrameTime = clock();
    }

}

struct ProfInfo {
    float avgCpu = 0;
    float avgReal = 0;
    clock_t startCpu = 0;
    std::chrono::steady_clock::time_point startReal = std::chrono::steady_clock::now();
};

static std::map<std::string, ProfInfo> profInfos;

void FrameInfo::begin(const std::string& tag) {
    if (!getDebugFlag(DebugFlags::tangram_infos)) { return; }
    auto& entry = profInfos[tag];
    entry.startReal = std::chrono::steady_clock::now();
    entry.startCpu = clock();
}

void FrameInfo::end(const std::string& tag) {
    if (!getDebugFlag(DebugFlags::tangram_infos)) { return; }
    auto& entry = profInfos[tag];
    auto endReal = std::chrono::steady_clock::now();
    float dtReal = std::chrono::duration<float>(endReal - entry.startReal).count()*1000.0f;
    entry.startReal = endReal;
    clock_t endCpu = clock();
    float dtCpu = TIME_TO_MS(entry.startCpu, endCpu);
    entry.startCpu = endCpu;

    float alpha = dtReal > 500 ? 1 : 0.1f;
    entry.avgReal = entry.avgReal*(1 - alpha) + dtReal*alpha;
    entry.avgCpu = entry.avgCpu*(1 - alpha) + dtCpu*alpha;
}

void FrameInfo::draw(RenderState& rs, const View& _view, Map& _map) {

    if (!getDebugFlag(DebugFlags::tangram_infos) && !getDebugFlag(DebugFlags::tangram_stats)) { return; }

    static std::deque<float> updatetime;
    static std::deque<float> rendertime;
    // Only compute average frame time every 60 frames
    float avgTimeRender = 0.f;
    float avgTimeCpu = 0.f;
    float avgTimeUpdate = 0.f;

    if (profInfos.empty() || getDebugFlag(DebugFlags::tangram_stats)) {
        static int cpt = 0;

        clock_t endCpu = clock();
        static float timeCpu[60] = { 0 };
        static float timeUpdate[60] = { 0 };
        static float timeRender[60] = { 0 };
        timeCpu[cpt] = TIME_TO_MS(s_startFrameTime, endCpu);

        if (updatetime.size() >= DEBUG_STATS_MAX_SIZE) {
            updatetime.pop_front();
        }
        if (rendertime.size() >= DEBUG_STATS_MAX_SIZE) {
            rendertime.pop_front();
        }

        rendertime.push_back(timeRender[cpt]);
        updatetime.push_back(timeUpdate[cpt]);

        // Force opengl to finish commands (for accurate frame time)
        GL::finish();

        s_endFrameTime = clock();
        timeRender[cpt] = TIME_TO_MS(s_startFrameTime, s_endFrameTime);

        if (++cpt == 60) { cpt = 0; }

        timeUpdate[cpt] = s_lastUpdateTime;

        for (int i = 0; i < 60; i++) {
            avgTimeRender += timeRender[i];
            avgTimeCpu += timeCpu[i];
            avgTimeUpdate += timeUpdate[i];
        }
        avgTimeRender /= 60;
        avgTimeCpu /= 60;
        avgTimeUpdate /= 60;
    }

    Scene& scene = *_map.getScene();
    TileManager& tileManager = *scene.tileManager();
    auto& tileCache = *tileManager.getTileCache();

    if (getDebugFlag(DebugFlags::tangram_infos)) {
        std::vector<std::string> debuginfos;

        auto& tiles = tileManager.getVisibleTiles();
        std::map<int, int> sourceCounts;
        size_t memused = 0, features = 0, nproxy = 0;
        for (const auto& tile : tiles) {
            memused += tile->getMemoryUsage();
            features += tile->getSelectionFeatures().size();
            ++sourceCounts[tile->sourceID()];
            if (tile->isProxy()) { ++nproxy; }
        }

        std::string countsStr;
        for (auto count : sourceCounts) {
            countsStr += " " + tileManager.getTileSource(count.first)->name()
                + ":" + std::to_string(count.second);
        }

        debuginfos.push_back(fstring("zoom:%.3f; base:%.3f (d:%.0fm, h:%.0fm); pitch:%.2fdeg",
            _view.getZoom(), _view.getBaseZoom(), _view.getPosition().z, _view.getEye().z, _view.getPitch()*180/M_PI));
        debuginfos.push_back(fstring("tiles:%d (proxy:%d);", tiles.size(), nproxy) + countsStr);
        debuginfos.push_back(fstring("selectable features:%d", features));
        debuginfos.push_back(fstring("markers:%d", scene.markerManager()->markers().size()));
        debuginfos.push_back(fstring("tile cache:%d (%dKB) (max:%dKB)", tileCache.getNumEntries(),
            tileCache.getMemoryUsage()/1024, tileCache.cacheSizeLimit()/1024));
        debuginfos.push_back(fstring("tile size:%dKB", memused / 1024));
#ifdef DEBUG
#ifdef TANGRAM_LINUX // || defined(TANGRAM_ANDROID) -- also supported on Android
        struct mallinfo2 mi;
        mi = mallinfo2();
        debuginfos.push_back(fstring("total memory (mallinfo2): %lluKB/%lluKB", mi.uordblks/1024, mi.arena/1024));
#elif defined(TANGRAM_WINDOWS)
        _HEAPINFO hinfo;
        hinfo._pentry = NULL;
        size_t heapblocks = 0, heaptotal = 0, heapused = 0;
        while (_heapwalk(&hinfo) == _HEAPOK) {
            ++heapblocks;
            heaptotal += hinfo._size;
            if (hinfo._useflag == _USEDENTRY) { heapused += hinfo._size; }
        }
        debuginfos.push_back(fstring("total memory (_heapwalk %llu blocks): %lluKB/%lluKB", heapblocks, heapused/1024, heaptotal/1024));
#endif
#endif
        debuginfos.push_back(fstring("pending downloads:%d (%dKB downloaded)",
            _map.getPlatform().activeUrlRequests(), _map.getPlatform().bytesDownloaded/1024));

        if (!profInfos.empty()) {
            end("_Frame");
            std::string reasons;
            if (_map.getScene()->labelManager()->needUpdate()) { reasons.append("l,"); }
            debuginfos.push_back(fstring("=== Frame %llu (%s) ===", Scene::frameCount%10000, reasons.c_str()));
            for (auto& entry : profInfos) {
                debuginfos.push_back(fstring("%s: %.3fms (CPU: %.3fms)",
                    entry.first.c_str(), entry.second.avgReal, entry.second.avgCpu));
            }
        } else {
            auto pos = _view.getPosition();
            debuginfos.push_back(fstring("avg frame cpu time:%.2fms", avgTimeCpu));
            debuginfos.push_back(fstring("avg frame render time:%.2fms", avgTimeRender));
            debuginfos.push_back(fstring("avg frame update time:%.2fms", avgTimeUpdate));
            debuginfos.push_back(fstring("pos: %f/%f", pos.x, pos.y));
            auto center = MapProjection::projectedMetersToLngLat(pos);
            debuginfos.push_back(fstring("LngLat:%f,%f", center.longitude, center.latitude));
            debuginfos.push_back(fstring("tilt:%.2fdeg", _view.getPitch() * 57.3));
        }

        TextDisplay::Instance().draw(rs, _view, debuginfos);
    }

    if (getDebugFlag(DebugFlags::tangram_stats)) {
        const int scale = 5 * _view.pixelScale();

        for (size_t i = 0; i < updatetime.size(); i++) {
            float tupdate = updatetime[i] * scale;
            float trender = rendertime[i] * scale;
            float offsetx = i * 4 * _view.pixelScale();

            Primitives::setColor(rs, 0xfff000);
            Primitives::drawLine(rs, glm::vec2(offsetx, 0), glm::vec2(offsetx, tupdate));
            Primitives::setColor(rs, 0x0000ff);
            Primitives::drawLine(rs, glm::vec2(offsetx, tupdate), glm::vec2(offsetx, tupdate + trender));
        }

        // Draw 16.6ms horizontal line
        Primitives::setColor(rs, 0xff0000);
        Primitives::drawLine(rs, glm::vec2(0.0, 16.6 * scale),
            glm::vec2(DEBUG_STATS_MAX_SIZE * 4 * _view.pixelScale() + 4, 16.6 * scale));
    }
}

}
