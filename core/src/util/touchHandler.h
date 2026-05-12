#pragma once

#include "view/view.h"
#include "util/touchListener.h"
#include "glm/vec2.hpp"

#include <memory>
#include <chrono>
#include <mutex>
#include <thread>

namespace Tangram {

// Touch action constants for internal use
enum class TouchAction {
    POINTER_1_DOWN = 0,
    POINTER_2_DOWN = 1,
    MOVE = 2,
    CANCEL = 3,
    POINTER_1_UP = 4,
    POINTER_2_UP = 5,
};

// Panning mode for dual pointer gestures (following Carto Mobile SDK)
enum class PanningMode {
    FREE = 0,          // Allows simultaneous rotation and scaling
    STICKY = 1,        // Separates rotate and scale gestures, but allows switching
    STICKY_FINAL = 2,  // Locks to first detected gesture until fingers lift
};

// Forward declarations
class Map;
class ClickHandlerWorker;

class TouchHandler : public std::enable_shared_from_this<TouchHandler> {
public:
    explicit TouchHandler(View& _view, Map* _map = nullptr);
    ~TouchHandler();

    // Must be called after construction (requires shared_from_this)
    void init();
    void deinit();

    // Main touch event handler
    void onTouchEvent(TouchAction action, const ScreenPos& screenPos1, const ScreenPos& screenPos2);

    // Update method for kinetic animations
    bool update(float _dt);

    // Cancel ongoing gestures
    void cancel();

    void setView(View& _view) { m_view = _view; }
    void setMap(Map* _map) { m_map = _map; }

    // Listeners
    void setMapClickListener(std::shared_ptr<MapClickListener> listener);
    void setMapInteractionListener(std::shared_ptr<MapInteractionListener> listener);
    void registerOnTouchListener(const std::shared_ptr<OnTouchListener>& listener);
    void unregisterOnTouchListener(const std::shared_ptr<OnTouchListener>& listener);

    // Enable/disable gestures
    void setGesturesEnabled(bool zoom, bool pan, bool doubleTap, bool doubleTapDrag, bool tilt, bool rotate);
    void setZoomEnabled(bool enabled) { m_zoomEnabled = enabled; }
    void setPanEnabled(bool enabled) { m_panEnabled = enabled; }
    void setDoubleTapEnabled(bool enabled) { m_doubleTapEnabled = enabled; }
    void setDoubleTapDragEnabled(bool enabled) { m_doubleTapDragEnabled = enabled; }
    void setTiltEnabled(bool enabled) { m_tiltEnabled = enabled; }
    void setRotateEnabled(bool enabled) { m_rotateEnabled = enabled; }

    bool isZoomEnabled() const { return m_zoomEnabled; }
    bool isPanEnabled() const { return m_panEnabled; }
    bool isDoubleTapEnabled() const { return m_doubleTapEnabled; }
    bool isDoubleTapDragEnabled() const { return m_doubleTapDragEnabled; }
    bool isTiltEnabled() const { return m_tiltEnabled; }
    bool isRotateEnabled() const { return m_rotateEnabled; }

    // DPI
    void setDpi(float dpi);
    float getDpi() const { return m_dpi; }

    // Panning mode
    void setPanningMode(PanningMode mode) { m_panningMode = mode; }
    PanningMode getPanningMode() const { return m_panningMode; }

    // Called from ClickHandlerWorker thread
    void click(const ScreenPos& screenPos, const std::chrono::milliseconds& duration);
    void longClick(const ScreenPos& screenPos, const std::chrono::milliseconds& duration);
    void doubleClick(const ScreenPos& screenPos, const std::chrono::milliseconds& duration);
    void dualClick(const ScreenPos& screenPos1, const ScreenPos& screenPos2,
                   const std::chrono::milliseconds& duration);
    void startSinglePointer(const ScreenPos& screenPos);
    void startDualPointer(const ScreenPos& screenPos1, const ScreenPos& screenPos2);

private:
    enum class GestureMode {
        SINGLE_POINTER_CLICK_GUESS = 0,
        DUAL_POINTER_CLICK_GUESS,
        SINGLE_POINTER_PAN,
        SINGLE_POINTER_ZOOM,
        DUAL_POINTER_GUESS,
        DUAL_POINTER_TILT,
        DUAL_POINTER_ROTATE,
        DUAL_POINTER_SCALE,
        DUAL_POINTER_FREE,
    };

    // Gesture methods
    void singlePointerPan(const ScreenPos& screenPos);
    void singlePointerZoom(const ScreenPos& screenPos);
    bool singlePointerZoomStop(const ScreenPos& screenPos);
    void doubleTapZoom(const ScreenPos& screenPos);

    void dualPointerGuess(const ScreenPos& screenPos1, const ScreenPos& screenPos2);
    void dualPointerTilt(const ScreenPos& screenPos1);
    void dualPointerPan(const ScreenPos& screenPos1, const ScreenPos& screenPos2,
                        bool rotate, bool scale);

    float calculateRotatingScalingFactor(const ScreenPos& screenPos1, const ScreenPos& screenPos2) const;

    glm::vec2 getTranslation(float _startX, float _startY, float _endX, float _endY);
    void setVelocity(float _zoom, glm::vec2 _pan);

    View& m_view;
    Map* m_map;

    float m_dpi;
    PanningMode m_panningMode;

    std::shared_ptr<MapClickListener> m_mapClickListener;
    std::shared_ptr<MapInteractionListener> m_mapInteractionListener;
    std::mutex m_listenersMutex;


    std::vector<std::shared_ptr<OnTouchListener> > m_onTouchListeners;
    mutable std::mutex m_onTouchListenersMutex;

    bool m_zoomEnabled;
    bool m_panEnabled;
    bool m_doubleTapEnabled;
    bool m_doubleTapDragEnabled;
    bool m_tiltEnabled;
    bool m_rotateEnabled;

    GestureMode m_gestureMode;
    int m_pointersDown;
    bool m_noDualPointerYet;

    ScreenPos m_prevScreenPos1;
    ScreenPos m_prevScreenPos2;

    glm::vec2 m_swipe1;
    glm::vec2 m_swipe2;

    std::chrono::steady_clock::time_point m_dualPointerReleaseTime;

    glm::vec2 m_velocityPan;
    float m_velocityZoom;

    std::shared_ptr<ClickHandlerWorker> m_clickHandlerWorker;
    std::thread m_clickHandlerThread;

    mutable std::recursive_mutex m_mutex;

    // Constants matching Carto
    static constexpr float GUESS_MAX_DELTA_Y_INCHES = 2.5f;
    static constexpr float GUESS_MIN_SWIPE_LENGTH_SAME_INCHES = 0.2f;
    static constexpr float GUESS_MIN_SWIPE_LENGTH_OPPOSITE_INCHES = 0.06f;
    static constexpr float GUESS_SWIPE_ABS_COS_THRESHOLD = 0.707f;
    static constexpr float GUESS_SWIPE_ZOOM_THRESHOLD = 0.06f;
    static constexpr float SCALING_FACTOR_THRESHOLD = 0.5f;
    static constexpr float ROTATION_FACTOR_THRESHOLD = 0.75f;
    static constexpr float ROTATION_SCALING_FACTOR_THRESHOLD_STICKY = 3.0f;
    static constexpr float INCHES_TO_ZOOM_DELTA = 1.0f;
    static constexpr float INCHES_TO_TILT_DELTA = 32.0f;
    static constexpr float DEFAULT_DPI = 160.0f;
    static constexpr std::chrono::milliseconds DUAL_KINETIC_HOLD_DURATION{100};
    static constexpr std::chrono::milliseconds DUAL_STOP_HOLD_DURATION{75};
    static constexpr std::chrono::milliseconds ZOOM_GESTURE_ANIMATION_DURATION{250};
};

} // namespace Tangram
