#!/bin/bash
# Usage: ANDROID_NDK=/path/to/ndk ./build.sh
set -e
: "${ANDROID_NDK:?Set ANDROID_NDK first}"
[ -f src/zygisk.hpp ] || { echo "src/zygisk.hpp missing. Copy it from github.com/topjohnwu/zygisk-module-sample (module/jni/zygisk.hpp)"; exit 1; }
mkdir -p module/zygisk
for ABI in arm64-v8a armeabi-v7a x86_64 x86; do
  cmake -S . -B build/$ABI -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
        -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release
  cmake --build build/$ABI
  cp build/$ABI/libbadhon_gp.so module/zygisk/$ABI.so
done
(cd module && rm -f ../badhon_game_profile.zip && zip -r ../badhon_game_profile.zip . -x '.*')
echo "Built: badhon_game_profile.zip"
