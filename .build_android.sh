#!/bin/sh
# Build Android APK from WSL - run with: wsl -d archlinux -- env -i HOME=/root sh ~/cold-start-nx/.build_android.sh
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk
export ANDROID_HOME=/mnt/c/Android/Sdk
export PATH=$JAVA_HOME/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd ~/cold-start-nx/android
./gradlew clean assembleDebug
