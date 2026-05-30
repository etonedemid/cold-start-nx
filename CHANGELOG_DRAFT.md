## What's New in v1.9.0

### Android Beta 

### Visuals
- Letterbox / unrendered area is now pure black instead of dark gray

### Build & Platform
- Android Gradle build fully integrated (gradlew, wrapper, launcher icons, proper CMake wiring)
- Fixed Switch build: `pkg-config` replaced with `PKG_CONFIG_PATH`-aware invocation + `--static` flag to pull all transitive SDL2 dependencies
- `touchcontrols` compiled only on Android (`#ifdef __ANDROID__`); no impact on PC/Switch builds
