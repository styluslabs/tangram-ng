#pragma once

#include "view/view.h"
#include "util/touchHandler.h"
#include <chrono>
#include <condition_variable>
#include <thread>
#include <memory>
#include <mutex>

namespace Tangram {

class ClickHandlerWorker {
public:
    explicit ClickHandlerWorker(float dpi);
    ~ClickHandlerWorker();

    void setComponents(const std::weak_ptr<TouchHandler>& touchHandler,
                       const std::shared_ptr<ClickHandlerWorker>& worker);

    bool isRunning() const;

    void init();
    void stop();

    void pointer1Down(const ScreenPos& screenPos);
    void pointer1Moved(const ScreenPos& screenPos);
    void pointer1Up();

    void pointer2Down(const ScreenPos& screenPos);
    void pointer2Moved(const ScreenPos& screenPos);
    void pointer2Up();

    void cancel();

    void operator()();

    // Configurable options (mirrors Carto's Options)
    float getDPI() const { return _dpi; }
    void setDPI(float dpi) { _dpi = dpi; }

    bool isClickTypeDetection() const { return _clickTypeDetection; }
    void setClickTypeDetection(bool v) { _clickTypeDetection = v; }

    bool isDoubleClickDetection() const { return _doubleClickDetection; }
    void setDoubleClickDetection(bool v) { _doubleClickDetection = v; }

    float getLongClickDuration() const { return _longClickDurationSeconds; }
    void setLongClickDuration(float seconds) { _longClickDurationSeconds = seconds; }

    float getDoubleClickMaxDuration() const { return _doubleClickMaxDurationSeconds; }
    void setDoubleClickMaxDuration(float seconds) { _doubleClickMaxDurationSeconds = seconds; }

private:
    enum ClickMode { NO_CLICK, LONG_CLICK, DOUBLE_CLICK, DUAL_CLICK };

    static const std::chrono::milliseconds DUAL_CLICK_BEGIN_DURATION;
    static const std::chrono::milliseconds DUAL_CLICK_END_DURATION;
    static const float DOUBLE_CLICK_TOLERANCE_INCHES;
    static const float MOVING_TOLERANCE_INCHES;

    void run();

    void afterLongClick();
    void afterDoubleClick();
    void afterDualClick();

    float _dpi;
    bool _clickTypeDetection;
    bool _doubleClickDetection;
    float _longClickDurationSeconds;
    float _doubleClickMaxDurationSeconds;

    bool _running;

    ClickMode _clickMode;

    std::chrono::steady_clock::time_point _startTime;

    int _pointersDown;

    ScreenPos _pointer1Down;
    ScreenPos _pointer1Moved;
    float _pointer1MovedSum;
    ScreenPos _pointer2Down;
    ScreenPos _pointer2Moved;
    float _pointer2MovedSum;

    bool _chosen;
    bool _canceled;

    std::weak_ptr<TouchHandler> _touchHandler;
    std::shared_ptr<ClickHandlerWorker> _worker;

    bool _stop;
    std::condition_variable _condition;
    mutable std::mutex _mutex;
};

} // namespace Tangram