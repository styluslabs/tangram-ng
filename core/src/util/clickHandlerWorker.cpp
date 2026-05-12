#include "util/clickHandlerWorker.h"
#include "util/touchHandler.h"

#include <cmath>
#include <thread>

namespace Tangram {

ClickHandlerWorker::ClickHandlerWorker(float dpi)
    : _dpi(dpi),
      _clickTypeDetection(true),
      _doubleClickDetection(true),
      _longClickDurationSeconds(0.5f),
      _doubleClickMaxDurationSeconds(0.4f),
      _running(false),
      _clickMode(NO_CLICK),
      _startTime(),
      _pointersDown(0),
      _pointer1Down(0.f, 0.f),
      _pointer1Moved(0.f, 0.f),
      _pointer1MovedSum(0.f),
      _pointer2Down(0.f, 0.f),
      _pointer2Moved(0.f, 0.f),
      _pointer2MovedSum(0.f),
      _chosen(false),
      _canceled(false),
      _stop(false) {
}

ClickHandlerWorker::~ClickHandlerWorker() {
}

void ClickHandlerWorker::setComponents(const std::weak_ptr<TouchHandler>& touchHandler,
                                       const std::shared_ptr<ClickHandlerWorker>& worker) {
    _touchHandler = touchHandler;
    _worker = worker;
}

bool ClickHandlerWorker::isRunning() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _running;
}

void ClickHandlerWorker::init() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_running) {
        return;
    }

    _startTime = std::chrono::steady_clock::now();

    _clickMode = LONG_CLICK;

    _pointersDown = 0;

    _pointer1MovedSum = 0;
    _pointer2MovedSum = 0;

    _chosen = false;
    _canceled = false;

    _running = true;

    _condition.notify_one();
}

void ClickHandlerWorker::stop() {
    std::lock_guard<std::mutex> lock(_mutex);
    _stop = true;
    _condition.notify_all();
}

void ClickHandlerWorker::pointer1Down(const ScreenPos& screenPos) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_chosen) {
        return;
    }

    _pointersDown++;

    if (_clickMode == DOUBLE_CLICK) {
        float dx = screenPos.x - _pointer1Down.x;
        float dy = screenPos.y - _pointer1Down.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist / _dpi < DOUBLE_CLICK_TOLERANCE_INCHES) {
            _chosen = true;
        }
    }

    _pointer1Down = screenPos;
    _pointer1Moved = _pointer1Down;
    _pointer1MovedSum = 0;
}

void ClickHandlerWorker::pointer1Moved(const ScreenPos& screenPos) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_chosen) {
        return;
    }

    _pointer1MovedSum += std::abs(screenPos.x - _pointer1Moved.x);
    _pointer1MovedSum += std::abs(screenPos.y - _pointer1Moved.y);
    _pointer1Moved = screenPos;

    if (_clickMode == LONG_CLICK) {
        if (_pointer1MovedSum / _dpi >= MOVING_TOLERANCE_INCHES) {
            _chosen = true;
            _canceled = true;
        }
    } else if (_clickMode == DUAL_CLICK) {
        if (_pointer1MovedSum / _dpi >= MOVING_TOLERANCE_INCHES && _pointersDown >= 2) {
            _chosen = true;
            _canceled = true;
        }
    }
}

void ClickHandlerWorker::pointer1Up() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_chosen) {
        return;
    }

    _pointersDown--;

    if (_clickMode == LONG_CLICK) {
        if (_clickTypeDetection && _doubleClickDetection) {
            _clickMode = DOUBLE_CLICK;
        } else {
            _chosen = true;
        }
    } else if (_clickMode == DUAL_CLICK) {
        if (_pointersDown == 0) {
            _chosen = true;
        }
    }
}

void ClickHandlerWorker::pointer2Down(const ScreenPos& screenPos) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_chosen) {
        return;
    }

    _pointersDown++;

    _pointer2Down = screenPos;
    _pointer2Moved = _pointer2Down;
    _pointer2MovedSum = 0;

    _clickMode = DUAL_CLICK;
    auto deltaTime = std::chrono::steady_clock::now() - _startTime;
    if (deltaTime > DUAL_CLICK_BEGIN_DURATION) {
        _chosen = true;
        _canceled = true;
    }
}

void ClickHandlerWorker::pointer2Moved(const ScreenPos& screenPos) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_chosen) {
        return;
    }

    _pointer2MovedSum += std::abs(screenPos.x - _pointer2Moved.x);
    _pointer2MovedSum += std::abs(screenPos.y - _pointer2Moved.y);
    _pointer2Moved = screenPos;

    if (_clickMode == DUAL_CLICK) {
        if (_pointer2MovedSum / _dpi >= MOVING_TOLERANCE_INCHES && _pointersDown == 2) {
            _chosen = true;
            _canceled = true;
        }
    }
}

void ClickHandlerWorker::pointer2Up() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_chosen) {
        return;
    }

    _pointersDown--;
}

void ClickHandlerWorker::cancel() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_chosen) {
        return;
    }

    _clickMode = NO_CLICK;
    _chosen = true;
}

void ClickHandlerWorker::operator()() {
    run();
    _worker.reset();
}

void ClickHandlerWorker::run() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_stop) {
                return;
            }
            if (!_running) {
                _condition.wait(lock);
            }
        }

        ClickMode clickMode = NO_CLICK;
        while (true) {
            {
                std::lock_guard<std::mutex> lock(_mutex);

                if (_stop) {
                    return;
                }
                if (_chosen) {
                    clickMode = _clickMode;
                    break;
                }

                auto deltaTime = std::chrono::steady_clock::now() - _startTime;
                auto longClickDuration = std::chrono::milliseconds(
                    static_cast<int>(_longClickDurationSeconds * 1000.0f));
                auto doubleClickMaxDuration = std::chrono::milliseconds(
                    static_cast<int>(_doubleClickMaxDurationSeconds * 1000.0f));

                switch (_clickMode) {
                case NO_CLICK:
                    _chosen = true;
                    break;
                case LONG_CLICK:
                    if (_clickTypeDetection && deltaTime >= longClickDuration) {
                        _chosen = true;
                    }
                    break;
                case DOUBLE_CLICK:
                    if (_clickTypeDetection && deltaTime >= doubleClickMaxDuration) {
                        _chosen = true;
                        _canceled = true;
                    }
                    break;
                case DUAL_CLICK:
                    if (_clickTypeDetection && deltaTime >= DUAL_CLICK_END_DURATION) {
                        _chosen = true;
                        _canceled = true;
                    }
                    break;
                }
            }
            std::this_thread::yield();
        }

        switch (clickMode) {
        case NO_CLICK:
            break;
        case LONG_CLICK:
            afterLongClick();
            break;
        case DOUBLE_CLICK:
            afterDoubleClick();
            break;
        case DUAL_CLICK:
            afterDualClick();
            break;
        }

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _running = false;
            _clickMode = NO_CLICK;
        }
    }
}

void ClickHandlerWorker::afterLongClick() {
    auto touchHandler = _touchHandler.lock();
    if (!touchHandler) {
        return;
    }

    std::unique_lock<std::mutex> lock(_mutex);
    bool canceled = _canceled;
    ScreenPos pointer1Down = _pointer1Down;
    ScreenPos pointer1Moved = _pointer1Moved;
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - _startTime);
    lock.unlock();

    if (canceled) {
        touchHandler->startSinglePointer(pointer1Moved);
        return;
    }

    touchHandler->longClick(pointer1Down, duration);
}

void ClickHandlerWorker::afterDoubleClick() {
    auto touchHandler = _touchHandler.lock();
    if (!touchHandler) {
        return;
    }

    std::unique_lock<std::mutex> lock(_mutex);
    bool canceled = _canceled;
    ScreenPos pointer1Down = _pointer1Down;
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - _startTime);
    lock.unlock();

    if (canceled) {
        touchHandler->click(pointer1Down, duration);
    } else {
        touchHandler->doubleClick(pointer1Down, duration);
    }
}

void ClickHandlerWorker::afterDualClick() {
    auto touchHandler = _touchHandler.lock();
    if (!touchHandler) {
        return;
    }

    std::unique_lock<std::mutex> lock(_mutex);
    bool canceled = _canceled;
    int pointersDown = _pointersDown;
    ScreenPos pointer1Down = _pointer1Down;
    ScreenPos pointer2Down = _pointer2Down;
    ScreenPos pointer1Moved = _pointer1Moved;
    ScreenPos pointer2Moved = _pointer2Moved;
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - _startTime);
    lock.unlock();

    if (canceled) {
        if (pointersDown == 1) {
            touchHandler->startSinglePointer(pointer1Moved);
        } else if (pointersDown >= 2) {
            touchHandler->startDualPointer(pointer1Moved, pointer2Moved);
        }
        return;
    }

    touchHandler->dualClick(pointer1Down, pointer2Down, duration);
}

const std::chrono::milliseconds ClickHandlerWorker::DUAL_CLICK_BEGIN_DURATION =
    std::chrono::milliseconds(100);
const std::chrono::milliseconds ClickHandlerWorker::DUAL_CLICK_END_DURATION =
    std::chrono::milliseconds(300);
const float ClickHandlerWorker::DOUBLE_CLICK_TOLERANCE_INCHES = 1.3f;
const float ClickHandlerWorker::MOVING_TOLERANCE_INCHES = 0.2f;

} // namespace Tangram