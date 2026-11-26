package com.styluslabs.tangram;

import androidx.annotation.Keep;

/**
 * Listener interface for map interaction events (panning, zooming, rotating, tilting).
 * The listener can be set with {@link MapController#setMapInteractionListener(MapInteractionListener)}.
 * The callback will be run on the main (UI) thread.
 */
@Keep
public interface MapInteractionListener {
    /**
     * Called when the user starts interacting with the map.
     * 
     * @param isPanning true if the interaction includes panning
     * @param isZooming true if the interaction includes zooming
     * @param isRotating true if the interaction includes rotating
     * @param isTilting true if the interaction includes tilting
     * @return true to consume all interaction events and prevent default behavior, false to allow default handling
     */
    boolean onMapInteraction(boolean isPanning, boolean isZooming, boolean isRotating, boolean isTilting);
}
