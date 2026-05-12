package com.styluslabs.tangram;

import androidx.annotation.Keep;

/**
 * Listener interface for map click events.
 * The listener can be set with {@link MapController#setMapClickListener(MapClickListener)}.
 * The callback will be run on the main (UI) thread.
 */
@Keep
public interface MapClickListener {
    
    /**
     * Types of click/tap gestures
     */
    enum ClickType {
        /**
         * A click caused by pressing down and then releasing the screen.
         */
        CLICK_TYPE_SINGLE,
        /**
         * A click caused by pressing down but not releasing the screen.
         */
        CLICK_TYPE_LONG,
        /**
         * A click caused by two fast consecutive taps on the screen.
         */
        CLICK_TYPE_DOUBLE,
        /**
         * A click caused by two simultaneous taps on the screen.
         */
        CLICK_TYPE_DUAL
    }
    
    /**
     * Called when the user taps on the map.
     * 
     * @param clickType The type of click that occurred
     * @param x The x screen coordinate where the tap occurred
     * @param y The y screen coordinate where the tap occurred
     * @return true to consume the event and prevent default behavior (centering), false to allow default handling
     */
    boolean onMapClick(ClickType clickType, float x, float y);
}
