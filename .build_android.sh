#!/bin/sh
# Build Android APK from WSL - run with: wsl -d archlinux -- env -i HOME=/root sh /mnt/z/cold-start-nx/.build_android.sh
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk
export ANDROID_HOME=/mnt/c/Android/Sdk
export PATH=$JAVA_HOME/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
cd /mnt/z/cold-start-nx/android
./gradlew clean assembleDebug
