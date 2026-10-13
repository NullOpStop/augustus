package com.github.Keriew.augustus;

import android.content.Context;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * Pen mode for styluses such as the Samsung S Pen.
 *
 * SDL only reports mouse buttons for SOURCE_MOUSE events, so a stylus is treated as a
 * finger: its side button is ignored and touch-mode rules apply (placements need a
 * confirmation tap, right click needs a second finger).
 *
 * While the pen is near the screen (hovering or touching), it drives the mouse instead:
 * the tip is the left button, the tip with the side button held is the right button and
 * hovering moves the cursor. Fingers are ignored meanwhile, so a resting palm does nothing.
 * Once the pen leaves hover range, fingers drag to scroll and pinch to zoom as before, but no
 * longer build: with a building selected, a finger drag scrolls the map instead.
 */
public class StylusSurface extends SDLSurface {
    // Android ends hover just before the tip touches down and restarts it after the tip
    // lifts, so wait a little before leaving pen mode on a hover exit.
    private static final long PEN_MODE_EXIT_DELAY_MS = 150;

    private static final String TAG = "AugustusPen";

    private final Runnable leavePenMode = () -> {
        penMode = false;
        Log.d(TAG, "pen mode off");
    };
    private boolean penMode;
    private int strokeButton;
    private int strokePointerId = -1;
    private boolean fingersForwarded;

    public StylusSurface(Context context, View.OnGenericMotionListener sdlListener) {
        super(context);
        setOnGenericMotionListener((v, event) -> {
            if (isStylus(event, 0)) {
                return onStylusHover(event);
            }
            return sdlListener.onGenericMotion(v, event);
        });
    }

    // Once a stylus is in use, fingers only scroll and zoom the map; the pen does the building
    private static native void stylusDetected();

    private static boolean isStylus(MotionEvent event, int index) {
        int toolType = event.getToolType(index);
        return toolType == MotionEvent.TOOL_TYPE_STYLUS || toolType == MotionEvent.TOOL_TYPE_ERASER;
    }

    private static int findStylus(MotionEvent event) {
        for (int i = 0; i < event.getPointerCount(); i++) {
            if (isStylus(event, i)) {
                return i;
            }
        }
        return -1;
    }

    private static boolean isSideButtonHeld(MotionEvent event) {
        // Before API 23 the side button was reported as BUTTON_SECONDARY
        return (event.getButtonState() &
                (MotionEvent.BUTTON_STYLUS_PRIMARY | MotionEvent.BUTTON_SECONDARY)) != 0;
    }

    private void enterPenMode(View v, MotionEvent event) {
        removeCallbacks(leavePenMode);
        if (penMode) {
            return;
        }
        penMode = true;
        stylusDetected();
        Log.d(TAG, "pen mode on");
        // End any finger gesture in progress so the game doesn't keep a stuck touch
        if (fingersForwarded) {
            MotionEvent cancel = MotionEvent.obtain(event);
            cancel.setAction(MotionEvent.ACTION_CANCEL);
            super.onTouch(v, cancel);
            cancel.recycle();
            fingersForwarded = false;
        }
    }

    private boolean onStylusHover(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_HOVER_ENTER:
            case MotionEvent.ACTION_HOVER_MOVE:
                enterPenMode(this, event);
                SDLActivity.onNativeMouse(0, MotionEvent.ACTION_HOVER_MOVE, event.getX(0), event.getY(0), false);
                return true;
            case MotionEvent.ACTION_HOVER_EXIT:
                if (strokeButton == 0) {
                    postDelayed(leavePenMode, PEN_MODE_EXIT_DELAY_MS);
                }
                return true;
            default:
                return false;
        }
    }

    @Override
    public boolean onTouch(View v, MotionEvent event) {
        int stylusIndex = findStylus(event);
        if (stylusIndex >= 0) {
            enterPenMode(v, event);
            handleStylusTouch(event, stylusIndex);
            return true;
        }
        if (penMode || strokeButton != 0) {
            // Palm or finger while the pen is in range
            return true;
        }
        int action = event.getActionMasked();
        fingersForwarded = action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL;
        return super.onTouch(v, event);
    }

    private void handleStylusTouch(MotionEvent event, int index) {
        int pointerId = event.getPointerId(index);
        float x = event.getX(index);
        float y = event.getY(index);
        int action = event.getActionMasked();
        boolean isStylusAction = event.getActionIndex() == index;

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (isStylusAction && strokeButton == 0) {
                    strokeButton = isSideButtonHeld(event) ? MotionEvent.BUTTON_SECONDARY : MotionEvent.BUTTON_PRIMARY;
                    strokePointerId = pointerId;
                    Log.d(TAG, strokeButton == MotionEvent.BUTTON_SECONDARY ? "right down" : "left down");
                    SDLActivity.onNativeMouse(strokeButton, MotionEvent.ACTION_DOWN, x, y, false);
                }
                break;
            case MotionEvent.ACTION_MOVE:
                if (strokeButton != 0 && pointerId == strokePointerId) {
                    SDLActivity.onNativeMouse(strokeButton, MotionEvent.ACTION_MOVE, x, y, false);
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                if (isStylusAction) {
                    endStroke(x, y);
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                endStroke(x, y);
                break;
            default:
                break;
        }
    }

    private void endStroke(float x, float y) {
        if (strokeButton == 0) {
            return;
        }
        SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP, x, y, false);
        strokeButton = 0;
        strokePointerId = -1;
        // Normally hover resumes right after the tip lifts; if it doesn't, the pen is gone
        removeCallbacks(leavePenMode);
        postDelayed(leavePenMode, PEN_MODE_EXIT_DELAY_MS);
    }
}
