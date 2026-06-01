// ─── COLD START - Android App Module ────────────────────────────────────────

plugins {
    id("com.android.application")
}

android {
    namespace = "com.coldstart.game"
    compileSdk = 34
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = "com.coldstart.game"
        minSdk = 21
        targetSdk = 34
        versionCode = 21
        versionName = "2.1.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
                    "-DANDROID_STL=c++_shared",
                    "-DSDL2IMAGE_VENDORED=ON",
                    "-DSDL2TTF_VENDORED=ON",
                    "-DSDL2MIXER_VENDORED=ON",
                    // Disable formats that require external source trees
                    // (ogg/vorbis need external/ogg, flac needs external/flac,
                    //  opus needs external/opus, mpg123 needs external/mpg123)
                    "-DSDL2MIXER_VORBIS=OFF",
                    "-DSDL2MIXER_FLAC=OFF",
                    "-DSDL2MIXER_OPUS=OFF",
                    "-DSDL2MIXER_MP3_MPG123=OFF",
                    // minimp3 is header-only and bundled inside SDL2_mixer source
                    "-DSDL2MIXER_MP3_MINIMP3=ON",
                    "-DSDL2MIXER_MOD=OFF",
                    "-DSDL2MIXER_WAVPACK=OFF",
                    "-DSDL2MIXER_MIDI=OFF"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("jni/CMakeLists.txt")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    sourceSets {
        getByName("main") {
            // Include SDL2's Java activity classes
            java.srcDirs(
                "src/main/java",
                "jni/SDL2/android-project/app/src/main/java"
            )
            // Game assets from romfs/
            assets.srcDirs("../../romfs")
        }
    }
}
