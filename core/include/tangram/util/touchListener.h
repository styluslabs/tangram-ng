#pragma once

#include <memory>
#include <functional>

namespace Tangram {

enum class TouchAction;

    // Screen position for touch coordinates
struct ScreenPos {
    float x;
    float y;
    
    ScreenPos() : x(0.f), y(0.f) {}
    ScreenPos(float _x, float _y) : x(_x), y(_y) {}
};

// Types of click/tap gestures
enum class ClickType {
    SINGLE = 0,  // A click caused by pressing down and then releasing the screen
    LONG = 1,    // A click caused by pressing down but not releasing the screen
    DOUBLE = 2,  // A click caused by two fast consecutive taps on the screen
    DUAL = 3     // A click caused by two simultaneous taps on the screen
};

// Map click listener interface
// Called when the user performs a tap on the map
class OnTouchListener {
public:
    virtual ~OnTouchListener() = default;
    
    virtual bool onTouchEvent(TouchAction action, const ScreenPos& screenPos1, const ScreenPos& screenPos2) = 0;
};

// Map click listener interface
// Called when the user performs a tap on the map
class MapClickListener {
public:
    virtual ~MapClickListener() = default;

    // Called when a tap occurs
    // Return true to consume the event and prevent default behavior (centering)
    // Return false to allow default handling
    virtual bool onMapClick(ClickType clickType, float x, float y) = 0;
};

// Map interaction listener interface  
// Called when the user is interacting with the map (panning, zooming, rotating, tilting)
class MapInteractionListener {
public:
    virtual ~MapInteractionListener() = default;
    
    // Called when map interaction starts
    // Return true to consume all interaction events and prevent default behavior
    // Return false to allow default handling
    virtual bool onMapInteraction(bool isPanning, bool isZooming, bool isRotating, bool isTilting) = 0;
};

}
