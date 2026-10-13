package com.github.Keriew.augustus;

import android.content.Context;
import android.view.MotionEvent;
import android.view.View;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * Makes a stylus (e.g. Samsung S Pen) behave like a mouse instead of a finger.
 *
 * SDL only reports mouse buttons for SOURCE_MOUSE events, so a stylus is treated as a
 * finger: its side button is ignored and touch-mode rules apply (placements need a
 * confirmation tap, right click needs a second finger). Here the pen tip is sent as the
 * left mouse button, the tip with the side button held as the right mouse button, and
 * hovering moves the mouse so building ghosts and tooltips follow the pen.
 */
public class StylusSurface extends SDLSurface {
    private int strokeButton;

    public StylusSurface(Context context, View.OnGenericMotionListener sdlListener) {
        super(context);
        setOnGenericMotionListener((v, event) -> {
            if (isStylus(event)) {
                return onStylusHover(event);
            }
            return sdlListener.onGenericMotion(v, event);
        });
    }

    private static boolean isStylus(MotionEvent event) {
        if (event.getPointerCount() != 1) {
            return false;
        }
        int toolType = event.getToolType(0);
        return toolType == MotionEvent.TOOL_TYPE_STYLUS || toolType == MotionEvent.TOOL_TYPE_ERASER;
    }

    private static boolean isSideButtonHeld(MotionEvent event) {
        // Before API 23 the side button was reported as BUTTON_SECONDARY
        return (event.getButtonState() &
                (MotionEvent.BUTTON_STYLUS_PRIMARY | MotionEvent.BUTTON_SECONDARY)) != 0;
    }

    private boolean onStylusHover(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_HOVER_ENTER:
            case MotionEvent.ACTION_HOVER_MOVE:
                SDLActivity.onNativeMouse(0, MotionEvent.ACTION_HOVER_MOVE, event.getX(0), event.getY(0), false);
                return true;
            default:
                return false;
        }
    }

    @Override
    public boolean onTouch(View v, MotionEvent event) {
        if (!isStylus(event) && strokeButton == 0) {
            return super.onTouch(v, event);
        }
        float x = event.getX(0);
        float y = event.getY(0);
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                strokeButton = isSideButtonHeld(event) ? MotionEvent.BUTTON_SECONDARY : MotionEvent.BUTTON_PRIMARY;
                SDLActivity.onNativeMouse(strokeButton, MotionEvent.ACTION_DOWN, x, y, false);
                break;
            case MotionEvent.ACTION_MOVE:
                if (strokeButton != 0) {
                    SDLActivity.onNativeMouse(strokeButton, MotionEvent.ACTION_MOVE, x, y, false);
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                if (strokeButton != 0) {
                    SDLActivity.onNativeMouse(0, MotionEvent.ACTION_UP, x, y, false);
                    strokeButton = 0;
                }
                break;
            default:
                break;
        }
        return true;
    }
}
