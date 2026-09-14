# Builds RestBed, the HTTP server library behind the MeOS REST/information server
# (restserver.cpp), from source on platforms without the prebuilt RestBed.lib.
#
# The headers shipped in code/restbed are those of RestBed 4.6 (with relative
# includes), so exactly that release is built. restserver.cpp includes them through
# "restbed/restbed" relative to its own directory; the library is built against the
# original headers of the same release.
#
# Sources are downloaded at configure time. For offline builds, point
# FETCHCONTENT_SOURCE_DIR_MEOS_RESTBED and FETCHCONTENT_SOURCE_DIR_MEOS_KASHMIR at
# unpacked copies of the two archives below.
#
# Deviations from the RestBed build:
#   - asio comes from the system (standalone asio, libasio-dev) instead of the bundled
#     asio 1.11. asio removed get_io_service(); the eight calls in two source files are
#     rewritten to get_executor().context() in a patched copy in the build directory.
#   - No SSL support (BUILD_SSL off): MeOS serves plain HTTP. BUILD_SSL only affects
#     RestBed's own source files, not its public headers.
#
# meos_add_restbed(<target>) creates the static library <target>.

include(FetchContent)

set(MEOS_RESTBED_URL "https://github.com/Corvusoft/restbed/archive/refs/tags/4.6.tar.gz")
set(MEOS_RESTBED_SHA256 "b2b75c39129bf667a3e1a9494b9cf0e3dcf6d2b081746918f0f6ba202f14a7e4")
# The kashmir revision RestBed 4.6 refers to as a submodule (UUIDs for web sockets).
set(MEOS_KASHMIR_URL "https://github.com/Corvusoft/kashmir-dependency/archive/2f3913f49c4ac7f9bff9224db5178f6f8f0ff3ee.tar.gz")
set(MEOS_KASHMIR_SHA256 "51239356e07c2ac935d0aa56ac3c760ad304757a4ca998b8b5d9e3602aa0975f")

function(meos_add_restbed target)
  # SOURCE_SUBDIR without a CMakeLists.txt: download only, RestBed's own build is not used.
  FetchContent_Declare(meos_restbed
    URL "${MEOS_RESTBED_URL}" URL_HASH "SHA256=${MEOS_RESTBED_SHA256}"
    SOURCE_SUBDIR meos-no-cmake)
  FetchContent_Declare(meos_kashmir
    URL "${MEOS_KASHMIR_URL}" URL_HASH "SHA256=${MEOS_KASHMIR_SHA256}"
    SOURCE_SUBDIR meos-no-cmake)
  FetchContent_MakeAvailable(meos_restbed meos_kashmir)

  find_path(MEOS_ASIO_INCLUDE_DIR asio.hpp REQUIRED)

  set(source_dir "${meos_restbed_SOURCE_DIR}/source")
  file(GLOB_RECURSE sources RELATIVE "${source_dir}" CONFIGURE_DEPENDS "${source_dir}/*.cpp")

  set(patched_dir "${CMAKE_CURRENT_BINARY_DIR}/restbed-patched")
  set(target_sources "")
  foreach(source IN LISTS sources)
    file(READ "${source_dir}/${source}" content)
    if(content MATCHES "get_io_service")
      # socket->get_io_service( ) becomes the io_context of the socket's executor.
      string(REGEX REPLACE
        "([A-Za-z_]+(\\( \\))?(->|\\.)(lowest_layer\\( \\)\\.)?)get_io_service\\( \\)"
        "static_cast< asio::io_context& >( \\1get_executor( ).context( ) )"
        content "${content}")
      set(patched "${patched_dir}/${source}")
      if(EXISTS "${patched}")
        file(READ "${patched}" old_content)
      else()
        set(old_content "")
      endif()
      if(NOT content STREQUAL old_content)
        file(WRITE "${patched}" "${content}")
      endif()
      list(APPEND target_sources "${patched}")
    else()
      list(APPEND target_sources "${source_dir}/${source}")
    endif()
  endforeach()

  add_library(${target} STATIC ${target_sources})
  target_compile_features(${target} PUBLIC cxx_std_17)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(${target} PRIVATE ASIO_STANDALONE)
  target_include_directories(${target} PRIVATE "${source_dir}")
  target_include_directories(${target} SYSTEM PRIVATE "${meos_kashmir_SOURCE_DIR}" "${MEOS_ASIO_INCLUDE_DIR}")
  # Third-party code: its warnings are not ours to fix.
  target_compile_options(${target} PRIVATE -w)
  find_package(Threads REQUIRED)
  target_link_libraries(${target} PUBLIC Threads::Threads)
endfunction()
