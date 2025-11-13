#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SUMO::libsumocpp" for configuration "Debug"
set_property(TARGET SUMO::libsumocpp APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(SUMO::libsumocpp PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/bin/libsumocppD.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/libsumocppD.dll"
  )

list(APPEND _cmake_import_check_targets SUMO::libsumocpp )
list(APPEND _cmake_import_check_files_for_SUMO::libsumocpp "${_IMPORT_PREFIX}/bin/libsumocppD.lib" "${_IMPORT_PREFIX}/bin/libsumocppD.dll" )

# Import target "SUMO::libtracicpp" for configuration "Debug"
set_property(TARGET SUMO::libtracicpp APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(SUMO::libtracicpp PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/bin/libtracicppD.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/bin/libtracicppD.dll"
  )

list(APPEND _cmake_import_check_targets SUMO::libtracicpp )
list(APPEND _cmake_import_check_files_for_SUMO::libtracicpp "${_IMPORT_PREFIX}/bin/libtracicppD.lib" "${_IMPORT_PREFIX}/bin/libtracicppD.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
