#!/usr/bin/env bash
# Kin Build Script
# Supports: arm64-v8a (primary), armeabi-v7a, x86, x86_64
# Reference: CoreTaskOptimizer build system

set -e

ABIS=(arm64-v8a)
BUILD_TYPE=${BUILD_TYPE:-Release}
ANDROID_PLATFORM=${ANDROID_PLATFORM:-android-29}

check_ndk() {
    echo "[*] Checking Android NDK..."
    if [[ -z "$ANDROID_NDK" ]]; then
        echo "[!] ANDROID_NDK environment variable not set"
        echo "[!] Please set it to your NDK path:"
        echo "    export ANDROID_NDK=/path/to/android-ndk"
        exit 1
    fi
    echo "[*] ANDROID_NDK: $ANDROID_NDK"

    if [[ ! -f "$ANDROID_NDK/build/cmake/android.toolchain.cmake" ]]; then
        echo "[!] Invalid NDK path - toolchain not found"
        echo "[!] Expected: $ANDROID_NDK/build/cmake/android.toolchain.cmake"
        exit 1
    fi
    echo "[*] NDK toolchain found"
}

build_abi() {
    local ABI=$1
    local BUILD_DIR="build/$ABI"

    echo ""
    echo "========================================"
    echo "[*] Building for $ABI..."
    echo "========================================"

    # Clean and create build directory
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    echo "[*] Configuring CMake... (NO_LICENSE=${NO_LICENSE:-OFF})"
    cmake \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM="$ANDROID_PLATFORM" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DNO_LICENSE="${NO_LICENSE:-OFF}" \
        -GNinja \
        ../..

    echo "[*] Building..."
    cmake --build . -j"$(nproc 2>/dev/null || echo 4)"

    # Create output directory
    mkdir -p "../../output/$ABI"

    # Copy the executable (二进制全名 Kinetic, 同 CMake OUTPUT_NAME)
    if [[ -f "bin/Kinetic" ]]; then
        cp "bin/Kinetic" "../../output/$ABI/"
        local size_kb=$(stat -c%s "../../output/$ABI/Kinetic" 2>/dev/null | awk '{print int($1/1024)}')
        echo "[*] Build successful for $ABI (${size_kb} KB)"
    else
        echo "[!] Error: Binary not found in build/$ABI/bin/"
        ls -la bin/ 2>/dev/null || true
        exit 1
    fi

    cd ../..
}

show_usage() {
    echo "Usage: ./build.sh [all|arm64-v8a|armeabi-v7a|x86|x86_64|clean]"
    echo ""
    echo "Commands:"
    echo "  all           Build all ABIs listed in ABIS array (default; 当前仅 arm64-v8a)"
    echo "  arm64-v8a     Build for ARM64 only (主目标, 已测试)"
    echo "  armeabi-v7a   Build for ARMv7 only (未纳入 all, 未实测)"
    echo "  x86           Build for x86 only"
    echo "  x86_64        Build for x86_64 only"
    echo "  clean         Clean all build artifacts"
    echo ""
    echo "Environment variables:"
    echo "  ANDROID_NDK       Path to Android NDK (required)"
    echo "  BUILD_TYPE        Release or Debug (default: Release)"
    echo "  ANDROID_PLATFORM  Minimum API level (default: android-29)"
    echo ""
    echo "Examples:"
    echo "  export ANDROID_NDK=/home/user/android-ndk"
    echo "  ./build.sh all"
    echo "  ./build.sh arm64-v8a"
    echo "  BUILD_TYPE=Debug ./build.sh arm64-v8a"
}

# Main
case "${1:-all}" in
    "help"|"-h"|"--help")
        show_usage
        exit 0
        ;;
    "all")
        check_ndk
        for ABI in "${ABIS[@]}"; do
            build_abi "$ABI"
        done
        echo ""
        echo "========================================"
        echo "[*] All builds completed successfully!"
        echo "[*] Output: output/"
        echo "========================================"
        ;;
    "clean")
        echo "[*] Cleaning build artifacts..."
        rm -rf build/ output/
        echo "[*] Clean complete"
        ;;
    arm64-v8a|armeabi-v7a|x86|x86_64)
        check_ndk
        build_abi "$1"
        ;;
    *)
        echo "[!] Invalid ABI: $1"
        show_usage
        exit 1
        ;;
esac
