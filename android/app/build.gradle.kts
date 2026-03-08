// ─── COLD START — Android App Module ────────────────────────────────────────

plugins {
    id("com.android.application")
}

android {
    namespace = "com.coldstart.game"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.coldstart.game"
        minSdk = 21
        targetSdk = 34
        versionCode = 1
        versionName = "0.9.6"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DSDL2IMAGE_VENDORED=ON",
                    "-DSDL2TTF_VENDORED=ON",
                    "-DSDL2MIXER_VENDORED=ON"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("jni/CMakeLists.txt")
            version = "3.22.1"
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
