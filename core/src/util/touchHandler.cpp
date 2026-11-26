#include "util/touchHandler.h"
#include "util/clickHandlerWorker.h"
#include "map.h"
#include "util/touchListener.h"
#include "glm/gtx/rotate_vector.hpp"

#include <cmath>
#include <algorithm>

// Damping factor for translation; reciprocal of the decay period in seconds
#define DAMPING_PAN 4.0f

// Damping factor for zoom; reciprocal of the decay period in seconds
#define DAMPING_ZOOM 6.0f

// Minimum translation at which momentum should stop (pixels per second)
#define THRESHOLD_STOP_PAN 24.f

// Minimum zoom at which momentum should stop (zoom levels per second)
#define THRESHOLD_STOP_ZOOM 0.3f

// Maximum pitch angle for pan limiting (degrees)
#define MAX_PITCH_FOR_PAN_LIMITING 75.0f

namespace Tangram {

TouchHandler::TouchHandler(View& _view, Map* _map)
    : m_view(_view),
      m_map(_map),
      m_dpi(DEFAULT_DPI),
      m_panningMode(PanningMode::FREE),
      m_zoomEnabled(true),
      m_panEnabled(true),
      m_doubleTapEnabled(true),
      m_doubleTapDragEnabled(true),
      m_tiltEnabled(true),
      m_rotateEnabled(true),
      m_gestureMode(GestureMode::SINGLE_POINTER_CLICK_GUESS),
      m_pointersDown(0),
      m_noDualPointerYet(true),
      m_prevScreenPos1(0.f, 0.f),
      m_prevScreenPos2(0.f, 0.f),
      m_swipe1(0.f, 0.f),
      m_swipe2(0.f, 0.f),
      m_dualPointerReleaseTime(std::chrono::steady_clock::now()),
      m_velocityPan(0.f, 0.f),
      m_velocityZoom(0.f) {
    m_clickHandlerWorker = std::make_shared<ClickHandlerWorker>(m_dpi);
    m_clickHandlerThread = std::thread(std::ref(*m_clickHandlerWorker));
}

TouchHandler::~TouchHandler() {
    deinit();
}

void TouchHandler::init() {
    m_clickHandlerWorker->setComponents(shared_from_this(), m_clickHandlerWorker);
}

void TouchHandler::deinit() {
    if (m_clickHandlerWorker) {
        m_clickHandlerWorker->stop();
        if (m_clickHandlerThread.joinable()) {
            m_clickHandlerThread.detach();
        }
        m_clickHandlerWorker.reset();
    }
}

void TouchHandler::setDpi(float dpi) {
    m_dpi = dpi;
    if (m_clickHandlerWorker) {
        m_clickHandlerWorker->setDPI(dpi);
    }
}

bool TouchHandler::update(float _dt) {
    auto velocityPanPixels = m_view.pixelsPerMeter() / m_view.pixelScale() * m_velocityPan;

    bool isFlinging = glm::length(velocityPanPixels) > THRESHOLD_STOP_PAN ||
                      std::abs(m_velocityZoom) > THRESHOLD_STOP_ZOOM;

    if (isFlinging) {
        m_velocityPan -= std::min(_dt * DAMPING_PAN, 1.f) * m_velocityPan;
        m_view.translate(_dt * m_velocityPan.x, _dt * m_velocityPan.y);

        m_velocityZoom -= std::min(_dt * DAMPING_ZOOM, 1.f) * m_velocityZoom;
        m_view.zoom(m_velocityZoom * _dt);
    }

    return isFlinging;
}

void TouchHandler::cancel() {
    setVelocity(0.f, glm::vec2(0.f, 0.f));
    if (m_clickHandlerWorker) {
        m_clickHandlerWorker->cancel();
    }
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_gestureMode = GestureMode::SINGLE_POINTER_CLICK_GUESS;
    m_pointersDown = 0;
}

void TouchHandler::setMapClickListener(std::shared_ptr<MapClickListener> listener) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);
    m_mapClickListener = listener;
}

void TouchHandler::setMapInteractionListener(std::shared_ptr<MapInteractionListener> listener) {
    std::lock_guard<std::mutex> lock(m_listenersMutex);
    m_mapInteractionListener = listener;
}

void TouchHandler::registerOnTouchListener(const std::shared_ptr<OnTouchListener>& listener) {
    {
        std::lock_guard<std::mutex> lock(m_onTouchListenersMutex);
        m_onTouchListeners.push_back(listener);
    }
}

void TouchHandler::unregisterOnTouchListener(const std::shared_ptr<OnTouchListener>& listener) {
    {
        std::lock_guard<std::mutex> lock(m_onTouchListenersMutex);
        m_onTouchListeners.erase(std::remove(m_onTouchListeners.begin(), m_onTouchListeners.end(), listener), m_onTouchListeners.end());
    }
}

void TouchHandler::setGesturesEnabled(bool zoom, bool pan, bool doubleTap, bool doubleTapDrag,
                                       bool tilt, bool rotate) {
    m_zoomEnabled = zoom;
    m_panEnabled = pan;
    m_doubleTapEnabled = doubleTap;
    m_doubleTapDragEnabled = doubleTapDrag;
    m_tiltEnabled = tilt;
    m_rotateEnabled = rotate;
}

glm::vec2 TouchHandler::getTranslation(float _startX, float _startY, float _endX, float _endY) {
    float elev = 0;
    m_view.screenPositionToLngLat(_startX, _startY, &elev);

    auto start = m_view.screenToGroundPlane(_startX, _startY, elev);
    auto end = m_view.screenToGroundPlane(_endX, _endY, elev);

    glm::vec2 dr = start - end;

    // prevent extreme panning when view is nearly horizontal
    if (m_view.getPitch() > MAX_PITCH_FOR_PAN_LIMITING * M_PI / 180.0) {
        float dpx = glm::length(glm::vec2(_startX - _endX, _startY - _endY)) / m_view.pixelsPerMeter();
        float dd = glm::length(dr);
        if (dd > dpx) {
            dr = dr * dpx / dd;
        }
    }
    return dr;
}

void TouchHandler::setVelocity(float _zoom, glm::vec2 _translate) {
    m_velocityPan = _translate;
    m_velocityZoom = _zoom;
}

// ---------------------------------------------------------------------------
// Methods called from ClickHandlerWorker thread
// ---------------------------------------------------------------------------

void TouchHandler::click(const ScreenPos& screenPos, const std::chrono::milliseconds& duration) {
    // Single click
    setVelocity(0.f, glm::vec2(0.f, 0.f));

    std::lock_guard<std::mutex> lock(m_listenersMutex);
    if (m_mapClickListener) {
        m_mapClickListener->onMapClick(ClickType::SINGLE, screenPos.x, screenPos.y);
    }
}

void TouchHandler::longClick(const ScreenPos& screenPos, const std::chrono::milliseconds& duration) {
    setVelocity(0.f, glm::vec2(0.f, 0.f));

    std::lock_guard<std::mutex> lock(m_listenersMutex);
    if (m_mapClickListener) {
        m_mapClickListener->onMapClick(ClickType::LONG, screenPos.x, screenPos.y);
    }
}

void TouchHandler::doubleClick(const ScreenPos& screenPos, const std::chrono::milliseconds& duration) {
    // Double-tap: either start zoom drag mode or fire double-tap zoom
    setVelocity(0.f, glm::vec2(0.f, 0.f));

    if (m_doubleTapDragEnabled && m_zoomEnabled) {
        // Notify interaction listener
        bool consumed = false;
        {
            std::lock_guard<std::mutex> lock(m_listenersMutex);
            if (m_mapInteractionListener) {
                consumed = m_mapInteractionListener->onMapInteraction(false, true, false, false);
            }
        }
        if (!consumed) {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_swipe1 = glm::vec2(0.f, 0.f);
            m_prevScreenPos1 = screenPos;
            m_gestureMode = GestureMode::SINGLE_POINTER_ZOOM;
            return;
        }
    }

    // Notify click listener; if not consumed, do default animated zoom
    bool consumed = false;
    {
        std::lock_guard<std::mutex> lock(m_listenersMutex);
        if (m_mapClickListener) {
            consumed = m_mapClickListener->onMapClick(ClickType::DOUBLE, screenPos.x, screenPos.y);
        }
    }
    if (!consumed && m_doubleTapEnabled && m_map) {
        m_map->handleDoubleTapGesture(screenPos.x, screenPos.y, false);
    }
}

void TouchHandler::dualClick(const ScreenPos& screenPos1, const ScreenPos& screenPos2,
                              const std::chrono::milliseconds& duration) {
    setVelocity(0.f, glm::vec2(0.f, 0.f));

    float x = (screenPos1.x + screenPos2.x) * 0.5f;
    float y = (screenPos1.y + screenPos2.y) * 0.5f;

    bool consumed = false;
    {
        std::lock_guard<std::mutex> lock(m_listenersMutex);
        if (m_mapClickListener) {
            consumed = m_mapClickListener->onMapClick(ClickType::DUAL, x, y);
        }
    }
    if (!consumed && m_doubleTapEnabled && m_map) {
        m_map->handleDoubleTapGesture(x, y, true);
    }
}

void TouchHandler::startSinglePointer(const ScreenPos& screenPos) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_prevScreenPos1 = screenPos;
    m_gestureMode = GestureMode::SINGLE_POINTER_PAN;
}

void TouchHandler::startDualPointer(const ScreenPos& screenPos1, const ScreenPos& screenPos2) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_swipe1 = glm::vec2(0.f, 0.f);
    m_swipe2 = glm::vec2(0.f, 0.f);
    m_prevScreenPos1 = screenPos1;
    m_prevScreenPos2 = screenPos2;
    m_gestureMode = GestureMode::DUAL_POINTER_GUESS;
}

// ---------------------------------------------------------------------------
// Gesture implementations
// ---------------------------------------------------------------------------

void TouchHandler::singlePointerPan(const ScreenPos& screenPos) {
    if (!m_panEnabled) {
        m_prevScreenPos1 = screenPos;
        return;
    }
    {
        std::lock_guard<std::mutex> lk(m_listenersMutex);
        if (m_mapInteractionListener) {
            m_mapInteractionListener->onMapInteraction(true, false, false, false);
        }
    }
    glm::vec2 translation = getTranslation(m_prevScreenPos1.x, m_prevScreenPos1.y,
                                            screenPos.x, screenPos.y);
    m_view.translate(translation);
    m_prevScreenPos1 = screenPos;
}

void TouchHandler::singlePointerZoom(const ScreenPos& screenPos) {
    if (!m_zoomEnabled) {
        m_prevScreenPos1 = screenPos;
        return;
    }
    {
        std::lock_guard<std::mutex> lk(m_listenersMutex);
        if (m_mapInteractionListener) {
            m_mapInteractionListener->onMapInteraction(false, true, false, false);
        }
    }

    float delta = INCHES_TO_ZOOM_DELTA * (screenPos.y - m_prevScreenPos1.y) / m_dpi;
    m_swipe1 += glm::vec2((screenPos.x - m_prevScreenPos1.x) / m_dpi,
                           (screenPos.y - m_prevScreenPos1.y) / m_dpi);
    m_view.zoom(delta);
    m_prevScreenPos1 = screenPos;
}

bool TouchHandler::singlePointerZoomStop(const ScreenPos& screenPos) {
    // If total swipe was small → instant double-tap zoom
    bool doZoom = (glm::length(m_swipe1) < GUESS_SWIPE_ZOOM_THRESHOLD);
    m_prevScreenPos1 = screenPos;
    return doZoom;
}

void TouchHandler::doubleTapZoom(const ScreenPos& screenPos) {
    setVelocity(0.f, glm::vec2(0.f, 0.f));

    // Call map click listener first
    {
        std::lock_guard<std::mutex> lock(m_listenersMutex);
        if (m_mapClickListener) {
            bool consumed = m_mapClickListener->onMapClick(ClickType::DOUBLE, screenPos.x, screenPos.y);
            if (consumed) {
                return; // Listener consumed the click
            }
        }
    }

    // Default behavior: animated zoom in by +1
    if (m_doubleTapEnabled && m_map) {
        m_map->handleDoubleTapGesture(screenPos.x, screenPos.y, false);
    }
}

float TouchHandler::calculateRotatingScalingFactor(const ScreenPos& screenPos1,
                                                    const ScreenPos& screenPos2) const {
    // Mirrors Carto's implementation exactly
    glm::vec2 prevDelta(m_prevScreenPos1.x - m_prevScreenPos2.x,
                        m_prevScreenPos1.y - m_prevScreenPos2.y);

    double factor = 0.0;
    glm::vec2 moveDelta(screenPos1.x - m_prevScreenPos1.x, screenPos1.y - m_prevScreenPos1.y);
    for (int i = 0; i < 2; i++) {
        float prevLen = glm::length(prevDelta);
        float moveLen = glm::length(moveDelta);
        if (prevLen > 0 && moveLen > 0) {
            float cosA = std::abs(glm::dot(moveDelta, prevDelta)) / moveLen / prevLen;
            cosA = std::min(1.0f, cosA);
            float sinA = std::sqrt(1.0f - cosA * cosA);
            float tanA = sinA / std::max(cosA, 1e-6f);
            factor += std::log(tanA);
        }
        moveDelta = glm::vec2(screenPos2.x - m_prevScreenPos2.x, screenPos2.y - m_prevScreenPos2.y);
    }
    return static_cast<float>(factor);
}

void TouchHandler::dualPointerGuess(const ScreenPos& screenPos1, const ScreenPos& screenPos2) {
    float dpi = m_dpi;
    float deltaY = std::abs(screenPos1.y - screenPos2.y) / dpi;

    if (deltaY > GUESS_MAX_DELTA_Y_INCHES) {
        m_gestureMode = GestureMode::DUAL_POINTER_FREE;
    } else {
        float prevSwipe1Length = glm::length(m_swipe1);
        float prevSwipe2Length = glm::length(m_swipe2);

        glm::vec2 tempSwipe1(screenPos1.x - m_prevScreenPos1.x, screenPos1.y - m_prevScreenPos1.y);
        m_swipe1 += tempSwipe1 * (1.0f / dpi);
        glm::vec2 tempSwipe2(screenPos2.x - m_prevScreenPos2.x, screenPos2.y - m_prevScreenPos2.y);
        m_swipe2 += tempSwipe2 * (1.0f / dpi);

        float swipe1Length = glm::length(m_swipe1);
        float swipe2Length = glm::length(m_swipe2);

        if (((swipe1Length > GUESS_MIN_SWIPE_LENGTH_OPPOSITE_INCHES && prevSwipe1Length > 0) ||
             (swipe2Length > GUESS_MIN_SWIPE_LENGTH_OPPOSITE_INCHES && prevSwipe2Length > 0))
            && m_swipe1.y * m_swipe2.y <= 0) {
            m_gestureMode = GestureMode::DUAL_POINTER_FREE;
        } else if ((swipe1Length > GUESS_MIN_SWIPE_LENGTH_SAME_INCHES ||
                    swipe2Length > GUESS_MIN_SWIPE_LENGTH_SAME_INCHES)
                   && m_swipe1.y * m_swipe2.y > 0) {
            // Check horizontal dominance (Carto's GUESS_SWIPE_ABS_COS_THRESHOLD)
            if ((swipe1Length > 0 && std::abs(m_swipe1.x / swipe1Length) > GUESS_SWIPE_ABS_COS_THRESHOLD) ||
                (swipe2Length > 0 && std::abs(m_swipe2.x / swipe2Length) > GUESS_SWIPE_ABS_COS_THRESHOLD)) {
                m_gestureMode = GestureMode::DUAL_POINTER_FREE;
            } else if (m_tiltEnabled) {
                m_gestureMode = GestureMode::DUAL_POINTER_TILT;
            }
        }
    }

    // Detect rotation/scaling if panning mode is not FREE
    if (m_gestureMode == GestureMode::DUAL_POINTER_FREE && m_panningMode != PanningMode::FREE) {
        float factor = calculateRotatingScalingFactor(screenPos1, screenPos2);
        if (factor > ROTATION_FACTOR_THRESHOLD) {
            m_gestureMode = GestureMode::DUAL_POINTER_ROTATE;
        } else if (factor < -SCALING_FACTOR_THRESHOLD) {
            m_gestureMode = GestureMode::DUAL_POINTER_SCALE;
        } else {
            m_gestureMode = GestureMode::DUAL_POINTER_GUESS;
            return;
        }
    }

    m_prevScreenPos1 = screenPos1;
    m_prevScreenPos2 = screenPos2;
}

void TouchHandler::dualPointerTilt(const ScreenPos& screenPos1) {
    if (!m_tiltEnabled) {
        m_prevScreenPos1 = screenPos1;
        return;
    }

    {
        std::lock_guard<std::mutex> lk(m_listenersMutex);
        if (m_mapInteractionListener) {
            m_mapInteractionListener->onMapInteraction(false, false, false, true);
        }
    }

    float scale = -INCHES_TO_TILT_DELTA / m_dpi;
    float deltaAngle = (screenPos1.y - m_prevScreenPos1.y) * scale * float(M_PI / 180.0);

    float maxpitch = std::min(75.f * float(M_PI / 180.0), m_view.getMaxPitch());
    float pitch0 = glm::clamp(m_view.getPitch(), 0.f, maxpitch);
    float pitch1 = glm::clamp(m_view.getPitch() + deltaAngle, 0.f, maxpitch);

    m_view.pitch(pitch1 - pitch0);
    m_prevScreenPos1 = screenPos1;
}

void TouchHandler::dualPointerPan(const ScreenPos& screenPos1, const ScreenPos& screenPos2,
                                   bool rotate, bool scale) {
    // Notify interaction listener
    {
        std::lock_guard<std::mutex> lk(m_listenersMutex);
        if (m_mapInteractionListener) {
            m_mapInteractionListener->onMapInteraction(m_panEnabled, scale && m_zoomEnabled,
                                                       rotate && m_rotateEnabled, false);
        }
    }

    // Calculate center points
    ScreenPos prevCenter((m_prevScreenPos1.x + m_prevScreenPos2.x) * 0.5f,
                         (m_prevScreenPos1.y + m_prevScreenPos2.y) * 0.5f);
    ScreenPos currCenter((screenPos1.x + screenPos2.x) * 0.5f,
                         (screenPos1.y + screenPos2.y) * 0.5f);

    // Pan
    if (m_panEnabled) {
        glm::vec2 translation = getTranslation(prevCenter.x, prevCenter.y,
                                               currCenter.x, currCenter.y);
        m_view.translate(translation);
    }

    // Scale
    if (scale && m_zoomEnabled) {
        float prevDist = std::hypot(m_prevScreenPos2.x - m_prevScreenPos1.x,
                                    m_prevScreenPos2.y - m_prevScreenPos1.y);
        float currDist = std::hypot(screenPos2.x - screenPos1.x,
                                    screenPos2.y - screenPos1.y);

        if (prevDist > 0.f && currDist > 0.f) {
            float scaleFactor = prevDist / currDist; // Carto uses prevDist/currDist

            float elev = 0.f;
            m_view.screenPositionToLngLat(currCenter.x, currCenter.y, &elev);
            auto start = m_view.screenToGroundPlane(currCenter.x, currCenter.y, elev);

            m_view.zoom(-std::log2(scaleFactor)); // negative because carto ratio is inverse

            auto end = m_view.screenToGroundPlane(currCenter.x, currCenter.y, elev);
            if (m_panEnabled) {
                m_view.translate(start - end);
            }
        }
    }

    // Rotate
    if (rotate && m_rotateEnabled) {
        float prevAngle = std::atan2(m_prevScreenPos2.y - m_prevScreenPos1.y,
                                     m_prevScreenPos2.x - m_prevScreenPos1.x);
        float currAngle = std::atan2(screenPos2.y - screenPos1.y,
                                     screenPos2.x - screenPos1.x);
        float rotation = currAngle - prevAngle;

        float elev = 0.f;
        m_view.screenPositionToLngLat(currCenter.x, currCenter.y, &elev);
        glm::vec2 offset = m_view.screenToGroundPlane(currCenter.x, currCenter.y, elev);

        if (m_panEnabled) {
            glm::vec2 translation_rot = offset - glm::rotate(offset, rotation);
            m_view.translate(translation_rot);
        }
        m_view.yaw(rotation);
    }

    m_prevScreenPos1 = screenPos1;
    m_prevScreenPos2 = screenPos2;
}

// ---------------------------------------------------------------------------
// Main touch event dispatcher — mirrors Carto's onTouchEvent exactly
// ---------------------------------------------------------------------------

void TouchHandler::onTouchEvent(TouchAction action, const ScreenPos& screenPos1,
                                 const ScreenPos& screenPos2) {
    std::vector<std::shared_ptr<OnTouchListener> > onTouchListeners;
    {
        std::lock_guard<std::mutex> lock(m_onTouchListenersMutex);
        onTouchListeners = m_onTouchListeners;
    }
    for (std::size_t i = onTouchListeners.size(); i-- > 0; ) {
        if (onTouchListeners[i]->onTouchEvent(action, screenPos1, screenPos2)) {
            return;
        }
    }

    std::unique_lock<std::recursive_mutex> lock(m_mutex);

    switch (action) {
    case TouchAction::POINTER_1_DOWN:
        if (!m_clickHandlerWorker->isRunning()) {
            m_clickHandlerWorker->init();
        }
        m_clickHandlerWorker->pointer1Down(screenPos1);
        m_noDualPointerYet = true;
        setVelocity(0.f, glm::vec2(0.f, 0.f));
        break;

    case TouchAction::POINTER_2_DOWN:
        m_noDualPointerYet = false;
        switch (m_gestureMode) {
        case GestureMode::SINGLE_POINTER_CLICK_GUESS:
            m_clickHandlerWorker->pointer2Down(screenPos2);
            m_gestureMode = GestureMode::DUAL_POINTER_CLICK_GUESS;
            break;
        case GestureMode::SINGLE_POINTER_PAN:
        case GestureMode::SINGLE_POINTER_ZOOM:
            startDualPointer(screenPos1, screenPos2);
            break;
        default:
            break;
        }
        break;

    case TouchAction::MOVE:
        switch (m_gestureMode) {
        case GestureMode::SINGLE_POINTER_CLICK_GUESS:
            m_clickHandlerWorker->pointer1Moved(screenPos1);
            break;
        case GestureMode::DUAL_POINTER_CLICK_GUESS:
            m_clickHandlerWorker->pointer1Moved(screenPos1);
            m_clickHandlerWorker->pointer2Moved(screenPos2);
            break;
        case GestureMode::SINGLE_POINTER_PAN: {
            auto deltaTime = std::chrono::steady_clock::now() - m_dualPointerReleaseTime;
            if (deltaTime >= DUAL_STOP_HOLD_DURATION) {
                singlePointerPan(screenPos1);
            }
            break;
        }
        case GestureMode::SINGLE_POINTER_ZOOM:
            singlePointerZoom(screenPos1);
            break;
        case GestureMode::DUAL_POINTER_GUESS:
            dualPointerGuess(screenPos1, screenPos2);
            break;
        case GestureMode::DUAL_POINTER_TILT:
            dualPointerTilt(screenPos1);
            break;
        case GestureMode::DUAL_POINTER_ROTATE:
        case GestureMode::DUAL_POINTER_SCALE:
            if (m_panningMode == PanningMode::STICKY) {
                float factor = calculateRotatingScalingFactor(screenPos1, screenPos2);
                if (factor > ROTATION_SCALING_FACTOR_THRESHOLD_STICKY) {
                    m_gestureMode = GestureMode::DUAL_POINTER_ROTATE;
                } else if (factor < -ROTATION_SCALING_FACTOR_THRESHOLD_STICKY) {
                    m_gestureMode = GestureMode::DUAL_POINTER_SCALE;
                }
            }
            dualPointerPan(screenPos1, screenPos2,
                           m_gestureMode == GestureMode::DUAL_POINTER_ROTATE,
                           m_gestureMode == GestureMode::DUAL_POINTER_SCALE);
            break;
        case GestureMode::DUAL_POINTER_FREE:
            dualPointerPan(screenPos1, screenPos2, true, true);
            break;
        }
        break;

    case TouchAction::CANCEL:
        m_pointersDown = 0;
        m_clickHandlerWorker->cancel();
        m_gestureMode = GestureMode::SINGLE_POINTER_CLICK_GUESS;
        setVelocity(0.f, glm::vec2(0.f, 0.f));
        break;

    case TouchAction::POINTER_1_UP:
        switch (m_gestureMode) {
        case GestureMode::SINGLE_POINTER_CLICK_GUESS:
            m_clickHandlerWorker->pointer1Up();
            break;
        case GestureMode::DUAL_POINTER_CLICK_GUESS:
            m_clickHandlerWorker->pointer1Up();
            m_gestureMode = GestureMode::SINGLE_POINTER_CLICK_GUESS;
            break;
        case GestureMode::SINGLE_POINTER_PAN:
            m_gestureMode = GestureMode::SINGLE_POINTER_CLICK_GUESS;
            if (m_noDualPointerYet) {
                setVelocity(0.f, m_velocityPan);
            } else {
                auto deltaTime = std::chrono::steady_clock::now() - m_dualPointerReleaseTime;
                if (deltaTime < DUAL_KINETIC_HOLD_DURATION) {
                    setVelocity(m_velocityZoom, m_velocityPan);
                }
            }
            break;
        case GestureMode::SINGLE_POINTER_ZOOM: {
            bool doZoom = singlePointerZoomStop(screenPos1);
            m_gestureMode = GestureMode::SINGLE_POINTER_CLICK_GUESS;
            if (doZoom) {
                lock.unlock();
                doubleTapZoom(screenPos1);
                lock.lock();
            }
            if (m_noDualPointerYet) {
                setVelocity(m_velocityZoom, glm::vec2(0.f, 0.f));
            }
            break;
        }
        case GestureMode::DUAL_POINTER_GUESS:
        case GestureMode::DUAL_POINTER_TILT:
        case GestureMode::DUAL_POINTER_ROTATE:
        case GestureMode::DUAL_POINTER_SCALE:
        case GestureMode::DUAL_POINTER_FREE:
            m_dualPointerReleaseTime = std::chrono::steady_clock::now();
            m_prevScreenPos1 = screenPos2;
            m_gestureMode = GestureMode::SINGLE_POINTER_PAN;
            break;
        }
        break;

    case TouchAction::POINTER_2_UP:
        switch (m_gestureMode) {
        case GestureMode::DUAL_POINTER_CLICK_GUESS:
            m_clickHandlerWorker->pointer2Up();
            m_gestureMode = GestureMode::SINGLE_POINTER_CLICK_GUESS;
            break;
        case GestureMode::DUAL_POINTER_GUESS:
        case GestureMode::DUAL_POINTER_TILT:
        case GestureMode::DUAL_POINTER_ROTATE:
        case GestureMode::DUAL_POINTER_SCALE:
        case GestureMode::DUAL_POINTER_FREE:
            m_dualPointerReleaseTime = std::chrono::steady_clock::now();
            m_prevScreenPos1 = screenPos1;
            m_gestureMode = GestureMode::SINGLE_POINTER_PAN;
            break;
        default:
            break;
        }
        break;
    }

    // Update pointer count
    if (action == TouchAction::POINTER_1_DOWN || action == TouchAction::POINTER_2_DOWN) {
        m_pointersDown = std::min(2, m_pointersDown + 1);
    } else if (action == TouchAction::POINTER_1_UP || action == TouchAction::POINTER_2_UP) {
        m_pointersDown = std::max(0, m_pointersDown - 1);
    } else if (action == TouchAction::CANCEL) {
        m_pointersDown = 0;
    }

    lock.unlock();
}

} // namespace Tangram
