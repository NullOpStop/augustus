package com.github.Keriew.augustus;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public class AugustusMainActivity extends SDLActivity {
    private static final int GET_FOLDER_RESULT = 500;

    @Override
    public void onStop() {
        super.onStop();
        releaseAssetManager();
        FileManager.clearCache();
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new StylusSurface(context, getMotionListener());
    }

    @Override
    protected String[] getLibraries() {
        String[] parentLibs = super.getLibraries();
        if (parentLibs.length > 0 && parentLibs[0].equals("SDL3")) {
             return new String[]{
                "SDL3",
                "SDL3_mixer",
                "augustus"
            };
        } else {
            return new String[]{
                "SDL2",
                "SDL2_mixer",
                "augustus"
            };
        }
    }

    @SuppressWarnings("unused")
    public void showDirectorySelection(boolean again) {
        startActivityForResult(DirectorySelectionActivity.newIntent(this, again), GET_FOLDER_RESULT);
    }

    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == GET_FOLDER_RESULT) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                FileManager.setBaseUri(data.getData());
            } else {
                FileManager.setBaseUri(Uri.EMPTY);
            }
            gotDirectory();
        } else {
            super.onActivityResult(requestCode, resultCode, data);
        }
    }

    @SuppressWarnings("unused")
    public float getScreenDensity() {
        return getResources().getDisplayMetrics().density;
    }

    private native void gotDirectory();
    private native void releaseAssetManager();
}
