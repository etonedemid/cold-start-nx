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
        versionCode = 25
        versionName = "3.3.1"

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
            java.srcDirs(
                "src/main/java",
                "jni/SDL2/android-project/app/src/main/java"
            )
            // Game assets from romfs/ + generated filelist
            assets.srcDirs("../../romfs", "src/main/generated-assets")
        }
    }
}

// Generate romfs_filelist.txt so the C++ code can enumerate assets to extract
tasks.register("generateRomfsFilelist") {
    val romfsDir = file("../../romfs")
    val outDir   = file("src/main/generated-assets")
    val outFile  = outDir.resolve("romfs_filelist.txt")
    inputs.dir(romfsDir)
    outputs.file(outFile)
    doLast {
        outDir.mkdirs()
        val lines = romfsDir.walkTopDown()
            .filter { it.isFile }
            .map  { it.relativeTo(romfsDir).path.replace("\\", "/") }
            .sorted()
        outFile.writeText(lines.joinToString("\n"))
        println("Generated romfs_filelist.txt (${lines.count()} files)")
    }
}
tasks.named("preBuild") { dependsOn("generateRomfsFilelist") }
