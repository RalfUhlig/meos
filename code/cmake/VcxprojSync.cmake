# Verifies that CMake and code/MeOS.vcxproj compile the same set of source files.
# Upstream MeOS releases only update MeOS.vcxproj; this check makes a new or removed
# source file visible on the next CMake run instead of failing later at link time.
#
# meos_check_vcxproj_sync(<path to MeOS.vcxproj> <source files known to CMake>...)

function(meos_check_vcxproj_sync vcxproj)
  set(cmake_sources ${ARGN})

  file(STRINGS "${vcxproj}" lines REGEX "<ClCompile Include=\"[^\"]+\"")
  set(vcxproj_sources "")
  foreach(line IN LISTS lines)
    string(REGEX REPLACE ".*<ClCompile Include=\"([^\"]+)\".*" "\\1" source "${line}")
    string(REPLACE "\\" "/" source "${source}")
    list(APPEND vcxproj_sources "${source}")
  endforeach()

  set(only_in_vcxproj ${vcxproj_sources})
  list(REMOVE_ITEM only_in_vcxproj ${cmake_sources})
  set(only_in_cmake ${cmake_sources})
  list(REMOVE_ITEM only_in_cmake ${vcxproj_sources})

  if(only_in_vcxproj OR only_in_cmake)
    list(JOIN only_in_vcxproj ", " a)
    list(JOIN only_in_cmake ", " b)
    message(FATAL_ERROR
      "Source lists of CMake and MeOS.vcxproj differ.\n"
      "  Only in MeOS.vcxproj: ${a}\n"
      "  Only in CMake:        ${b}\n"
      "Assign new files to one of the MEOS_*_SOURCES lists in code/CMakeLists.txt.")
  endif()
endfunction()
