#!/bin/bash
rm -rf build_debug
mkdir build_debug
cd build_debug
cmake .. -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles"
make -j8
cp -f TrafficFlow/TrafficFlow ..
cd ..
rm -rf build_debug
