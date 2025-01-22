#!/bin/bash

# Function to display usage instructions
usage() {
    echo "Usage: $0 [platform] [shipping]"
    echo "platform: linux"
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
