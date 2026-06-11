#!/bin/sh
# Detect Android SDK: prefer native Linux path, fall back to WSL Windows mount.
if [ -d "$HOME/Android/Sdk" ]; then
    export ANDROID_HOME="$HOME/Android/Sdk"
elif [ -d "/opt/android-sdk" ]; then
    export ANDROID_HOME="/opt/android-sdk"
elif [ -d "/mnt/c/Android/Sdk" ]; then
    export ANDROID_HOME="/mnt/c/Android/Sdk"
else
    echo "ERROR: Android SDK not found. Install it to ~/Android/Sdk or set ANDROID_HOME." >&2
    exit 1
fi

# Detect Java 17 (required by Android Gradle plugin).
for jdir in \
    /usr/lib/jvm/java-17-openjdk \
    /usr/lib/jvm/java-17-openjdk-amd64 \
    /usr/lib/jvm/temurin-17 \
    /usr/lib/jvm/default-runtime; do
    if [ -x "$jdir/bin/java" ]; then
        export JAVA_HOME="$jdir"
        break
    fi
done
if [ -z "$JAVA_HOME" ]; then
    echo "ERROR: Java 17 not found. Install openjdk-17." >&2
    exit 1
fi

export PATH=$JAVA_HOME/bin:$PATH
cd ~/cold-start-nx/android
./gradlew clean assembleRelease
