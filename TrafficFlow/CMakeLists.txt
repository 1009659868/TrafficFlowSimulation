cmake_minimum_required(VERSION 3.14.0)

project(TrafficFlow)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
# -fPIC 生成位置无关代码，用于动态库
# -fPIE 生成位置无关代码，用于可执行程序
# -Wno-inconsistent-missing-override  关闭某一个警告，这边为inconsistent-missing-override
# 上面这些应该都没有，都删除了
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17 -pthread")
#set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")

set(Srcs "")
file(GLOB_RECURSE CurrentDirSrcs ${CMAKE_CURRENT_LIST_DIR}/*.cpp)
file(GLOB HdmapSrcs ${CMAKE_CURRENT_LIST_DIR}/../hdmap/*.cpp)
list(APPEND Srcs ${CurrentDirSrcs} ${HdmapSrcs})

add_executable(TrafficFlow ${Srcs})

# proj的xyz和经纬度转换有多个版本，当前只能使用_PROJ_OLD_
add_compile_definitions(_PROJ_OLD_)
#add_compile_definitions(_PROJ_NEW_)

target_include_directories(TrafficFlow PRIVATE "${TrafficFlow_PATH_ROOT}/dependencies/sumo/linux/sumo/include")
target_include_directories(TrafficFlow PRIVATE "${TrafficFlow_PATH_ROOT}/dependencies/redis/hiredis-install/include")
target_include_directories(TrafficFlow PRIVATE "${TrafficFlow_PATH_ROOT}/dependencies/redis/redis-plus-plus-install/include")
target_include_directories(TrafficFlow PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_include_directories(TrafficFlow PRIVATE "${TrafficFlow_PATH_ROOT}/dependencies/hdmap/include")
target_include_directories(TrafficFlow PRIVATE "${TrafficFlow_PATH_ROOT}/dependencies/hdmap/include/public")

target_include_directories(TrafficFlow PRIVATE "${TrafficFlow_PATH_ROOT}/hdmap")


target_link_libraries(TrafficFlow ${TrafficFlow_PATH_ROOT}dependencies/lib/libsumocpp.so)
target_link_libraries(TrafficFlow ${TrafficFlow_PATH_ROOT}dependencies/lib/libtracicpp.so)
target_link_libraries(TrafficFlow ${TrafficFlow_PATH_ROOT}dependencies/lib/libhiredis.so.1)
target_link_libraries(TrafficFlow ${TrafficFlow_PATH_ROOT}dependencies/lib/libredis++.so.1)
target_link_libraries(TrafficFlow ${TrafficFlow_PATH_ROOT}dependencies/lib/libSSD.so)
target_link_libraries(TrafficFlow ${TrafficFlow_PATH_ROOT}dependencies/lib/libHDMapModule.so)
target_link_libraries(TrafficFlow ${TrafficFlow_PATH_ROOT}dependencies/lib/libproj.so.15)

#-Wl,-rpath,设置运行时查找lib的目录，使用相对路径
#将TrafficFlow拷贝到TrafficFlow/Build目录
#set_target_properties(TrafficFlow PROPERTIES LINK_FLAGS "-Wl,-rpath,../dependencies/lib")


