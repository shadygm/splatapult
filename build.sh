#!/bin/bash

# Function to display usage instructions
usage() {
    echo "Usage: $0 [platform] [shipping]"
    echo "platform: windows | linux | quest"
    echo "shipping: optional, use 'shipping' to create a shipping build"
    exit 1
}

# Check for platform argument
if [ -z "$1" ]; then
    usage
fi

PLATFORM="$1"
SHIPPING=""
if [ "$2" == "shipping" ]; then
    SHIPPING="-DSHIPPING=ON"
fi

# General steps
init_vcpkg() {
    echo "Initializing vcpkg submodule..."
    git submodule init && git submodule update
}

build_windows() {
    echo "Building for Windows..."
    init_vcpkg
    cd vcpkg || exit
    ./bootstrap-vcpkg.bat
    cd .. || exit
    mkdir -p build && cd build || exit
    cmake .. -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake $SHIPPING
    cmake --build . --config=Release
}

build_linux() {
    echo "Building for Linux..."
    echo "Installing dependencies..."
    sudo apt-get install -y clang cmake freeglut3-dev libopenxr-dev meson python3-jinja2
    init_vcpkg
    cd vcpkg || exit
    ./bootstrap-vcpkg.sh
    cd .. || exit
    mkdir -p build && cd build || exit
    cmake .. -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake $SHIPPING
    cmake --build . --config=Release
}

build_quest() {
    echo "Building for Meta Quest..."
    echo "Installing required packages with vcpkg..."
    vcpkg install glm:arm64-android
    vcpkg install libpng:arm64-android
    vcpkg install nlohmann-json:arm64-android

    echo "Set ANDROID_VCPKG_DIR to point to vcpkg/installed/arm64-android."
    echo "Download and setup Meta OpenXR Mobile SDK 59.0."
    echo "Copy the ovr_openxr_mobile_sdk_59.0 dir into the meta-quest directory."
    echo "Copy the meta-quest/splatapult dir to ovr_openxr_mobile_sdk_59.0/XrSamples/splatapult."
    echo "Open the project in Android Studio and sync/build."
}

case "$PLATFORM" in
    windows)
        build_windows
        ;;
    linux)
        build_linux
        ;;
    quest)
        build_quest
        ;;
    *)
        usage
        ;;
esac

echo "Build script complete."
