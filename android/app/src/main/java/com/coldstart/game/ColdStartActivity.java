package com.coldstart.game;

import org.libsdl.app.SDLActivity;

/**
 * Thin wrapper around SDL2's SDLActivity.
 * Specifies which native shared libraries to load at startup.
 */
public class ColdStartActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[]{
            "SDL2",
            "SDL2_image",
            "SDL2_ttf",
            "SDL2_mixer",
            "main"
        };
    }
}
