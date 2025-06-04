#include "util/inputHandler.h"

#include "glm/gtx/rotate_vector.hpp"
#include "glm/vec2.hpp"
#include <cmath>

// Damping factor for translation; reciprocal of the decay period in seconds
#define DAMPING_PAN 4.0f

// Damping factor for zoom; reciprocal of the decay period in seconds
#define DAMPING_ZOOM 6.0f

// Minimum translation at which momentum should start (pixels per second)
#define THRESHOLD_START_PAN 350.f

// Minimum translation at which momentum should stop (pixels per second)
#define THRESHOLD_STOP_PAN 24.f

// Minimum zoom at which momentum should start (zoom levels per second)
#define THRESHOLD_START_ZOOM 1.f

// Minimum zoom at which momentum should stop (zoom levels per second)
#define THRESHOLD_STOP_ZOOM 0.3f

namespace Tangram {

InputHandler::InputHandler(View& _view) : m_view(_view) {}

bool InputHandler::update(float _dt) {

    auto velocityPanPixels = m_view.pixelsPerMeter() / m_view.pixelScale() * m_velocityPan;

    bool isFlinging = glm::length(velocityPanPixels) > THRESHOLD_STOP_PAN ||
                      std::abs(m_velocityZoom) > THRESHOLD_STOP_ZOOM;

    if (isFlinging) {

        m_velocityPan -= min(_dt * DAMPING_PAN, 1.f) * m_velocityPan;
        m_view.translate(_dt * m_velocityPan.x, _dt * m_velocityPan.y);

        m_velocityZoom -= min(_dt * DAMPING_ZOOM, 1.f) * m_velocityZoom;
        m_view.zoom(m_velocityZoom * _dt);
    }

    return isFlinging;
}

void InputHandler::handleTapGesture(float _posX, float _posY) {
    cancelFling();

    float viewCenterX = 0.5f * m_view.getWidth();
    float viewCenterY = 0.5f * m_view.getHeight();

    auto center = m_view.screenToGroundPlane(viewCenterX, viewCenterY);
    auto pos = m_view.screenToGroundPlane(_posX, _posY);

    m_view.translate(pos - center);
}

void InputHandler::handleDoubleTapGesture(float _posX, float _posY) {
    handlePinchGesture(_posX, _posY, 2.f, 0.f);
}

glm::vec2 InputHandler::getTranslation(float _startX, float _startY, float _endX, float _endY) {

    float elev = 0;
    m_view.screenPositionToLngLat(_startX, _startY, &elev);

    auto start = m_view.screenToGroundPlane(_startX, _startY, elev);
    auto end = m_view.screenToGroundPlane(_endX, _endY, elev);

    glm::vec2 dr = start - end;

    // prevent extreme panning when view is nearly horizontal
    if (m_view.getPitch() > 75.0*M_PI/180) {
        float dpx = glm::length(glm::vec2(_startX - _endX, _startY - _endY))/m_view.pixelsPerMeter();
        float dd = glm::length(dr);
        if (dd > dpx) {
          dr = dr*dpx/dd;
        }
    }
    return dr;
}

void InputHandler::handlePanGesture(float _startX, float _startY, float _endX, float _endY) {
    cancelFling();
    m_view.translate(getTranslation(_startX, _startY, _endX, _endY));
}

void InputHandler::handleFlingGesture(float _posX, float _posY, float _velocityX, float _velocityY) {

    if (glm::length(glm::vec2(_velocityX, _velocityY)) / m_view.pixelScale() <= THRESHOLD_START_PAN) {
        return;
    }

    cancelFling();

    const static float epsilon = 0.0167f;
    float endX = _posX + epsilon * _velocityX;
    float endY = _posY + epsilon * _velocityY;
    glm::vec2 dr = getTranslation(_posX, _posY, endX, endY);
    setVelocity(0.f, dr / epsilon);
}

void InputHandler::handlePinchGesture(float _posX, float _posY, float _scale, float _velocity) {
    cancelFling();

    if (_scale <= 0.f) { return; }

    // point at screen position (_posX, _posY) should remain fixed
    float elev;
    m_view.screenPositionToLngLat(_posX, _posY, &elev);
    auto start = m_view.screenToGroundPlane(_posX, _posY, elev);

    m_view.zoom(std::log2(_scale));  //log(_scale) * invLog2);

    auto end = m_view.screenToGroundPlane(_posX, _posY, elev);
    m_view.translate(start - end);

    // previous approach - before 3D terrain added
    //m_view.screenToGroundPlane(_posX, _posY);
    //float s = pow(2, m_view.getZoom() - z) - 1;
    //m_view.translate(s * _posX, s * _posY);

    // Take the derivative of zoom as a function of scale:
    // z(s) = log2(s) + C
    // z'(s) = s' / s / log(2)
    static float invLog2 = 1 / log(2);
    float vz = _velocity / _scale * invLog2;
    if (std::abs(vz) >= THRESHOLD_START_ZOOM) {
        setVelocity(vz, glm::vec2(0.f));
    }
}

void InputHandler::handleRotateGesture(float _posX, float _posY, float _radians) {
    cancelFling();

    glm::vec2 offset;
    // at large tilt (pitch), we want to rotate about eye position instead of look at position, but
    //  let user control this by passing NAN for _posX, _posY
    if (std::isnan(_posX) || std::isnan(_posY)) {
        offset = glm::vec2(m_view.getEye());
    } else {
        float elev = 0;
        m_view.screenPositionToLngLat(_posX, _posY, &elev);
        // Get vector from center of rotation to view center
        offset = m_view.screenToGroundPlane(_posX, _posY, elev);
    }

    // Rotate vector by gesture rotation and apply difference as translation
    glm::vec2 translation = offset - glm::rotate(offset, _radians);
    m_view.translate(translation);

    m_view.yaw(_radians);
}

void InputHandler::handleShoveGesture(float _distance) {
    cancelFling();

    float angle = -M_PI * _distance / m_view.getHeight();

    // In 3D mode, adjust zoom and position to pivot about terrain at screen center (for pitch < 75 deg)
    float maxpitch = std::min(75.f*float(DEG_TO_RAD), m_view.getMaxPitch());
    float pitch0 = glm::clamp(m_view.getPitch(), 0.f, maxpitch);
    float pitch1 = glm::clamp(m_view.getPitch() + angle, 0.f, maxpitch);

    float _posX = 0.5f * m_view.getWidth();
    float _posY = 0.5f * m_view.getHeight();
    float elev = 0;
    m_view.screenPositionToLngLat(_posX, _posY, &elev);
    if (pitch0 != pitch1 && elev > 0) {
        auto viewPos = m_view.getPosition();

        float dl = elev*(1/std::cos(pitch1) - 1/std::cos(pitch0));
        m_view.setBaseZoom(m_view.getBaseZoom() - std::log2(1 + dl/viewPos.z));

        float dpos = elev*(std::tan(pitch1) - std::tan(pitch0));
        m_view.translate(glm::rotate(glm::vec2(0.f, dpos), m_view.getYaw()));
    }

    m_view.pitch(angle);
}

void InputHandler::cancelFling() {
    setVelocity(0.f, { 0.f, 0.f });
}

void InputHandler::setVelocity(float _zoom, glm::vec2 _translate) {
    // setup deltas for momentum on gesture
    m_velocityPan = _translate;
    m_velocityZoom = _zoom;
}

}
