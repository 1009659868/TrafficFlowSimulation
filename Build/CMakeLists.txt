cmake_minimum_required(VERSION 3.8.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
IF (UNIX)
	#set(CMAKE_C_COMPILER "/usr/bin/clang")
	#set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
	#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -I/usr/include/libcxxabi")
	#set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -stdlib=libc++ -lc++abi")
	set(TrafficFlow_PATH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../)
ENDIF()

add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)
 
# top project
project(TrafficFlow VERSION 3.0.0 LANGUAGES C CXX)

set(THREADS_PREFER_PTHREAD_FLAG ON)

# global settings
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_LIST_DIR}/modules)
set(TrafficFlow_PATH_ROOT ${CMAKE_CURRENT_LIST_DIR}/../)
set(PLATFORMRL_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

function(SetDefaultTargetProperties target)
	set_target_properties(${target} PROPERTIES
		CXX_STANDARD 14
	)
		set_target_properties(${target} PROPERTIES
		ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
		LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
	)
endfunction()

add_subdirectory(${PLATFORMRL_ROOT}/TrafficFlow TrafficFlow)


