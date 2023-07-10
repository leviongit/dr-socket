#!/bin/bash

BUILD_TYPE="Debug"
OS_TYPE="linux-raspberrypi"

cmake -S. -Bcmake-build-${OS_TYPE}-${BUILD_TYPE} -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DRASPBERRY_PI=TRUE -DCMAKE_TOOLCHAIN_FILE=tools/raspberrypi.toolchain.cmake
cmake --build cmake-build-${OS_TYPE}-${BUILD_TYPE} -j 8