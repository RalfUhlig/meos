# MeOS build definition for Windows (MSVC) and Linux (GCC/Clang).
#
# code/MeOS.vcxproj remains a second build path on Windows. cmake/VcxprojSync.cmake
# fails the configure step if both definitions list different source files.
#
# Linux port, stage 0: only meos_core (the platform independent domain logic) is
# built on Linux. The remaining groups follow in later stages, see plans/linux-port.md.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CaseAliases)
include(CheckCXXCompilerFlag)
include(CheckIPOSupported)
include(VcxprojSync)

option(MEOS_SANITIZE "Build Debug configurations with AddressSanitizer and UBSan (GCC/Clang)" ON)

# ---------------------------------------------------------------------------
# Source groups
# ---------------------------------------------------------------------------

# Domain model, results, lists, import/export. Portable apart from compatibility shims.
set(MEOS_CORE_SOURCES
  binencoder.cpp cardsystem.cpp classconfiginfo.cpp csvparser.cpp generalresult.cpp
  HTMLWriter.cpp importformats.cpp iof30interface.cpp localizer.cpp machinecontainer.cpp
  MeOSFeatures.cpp meosversion.cpp meos_util.cpp metalist.cpp oBase.cpp oCard.cpp
  oClass.cpp oClub.cpp oControl.cpp oCourse.cpp oDataContainer.cpp oEvent.cpp
  oEventDraw.cpp oEventResult.cpp oEventSpeaker.cpp oEventSQL.cpp oevent_transfer.cpp
  oFreeImport.cpp oFreePunch.cpp oImportExport.cpp oListInfo.cpp oPunch.cpp oReport.cpp
  oRunner.cpp oTeam.cpp oTeamEvent.cpp parser.cpp pdfwriter.cpp qualification_final.cpp
  random.cpp recorder.cpp RunnerDB.cpp TimeStamp.cpp utm.cpp xmlparser.cpp)

# Tabs and editors. Built on the gdioutput API only (stage 1 on Linux).
set(MEOS_UI_SOURCES
  liveresult.cpp listeditor.cpp methodeditor.cpp newcompetition.cpp prefseditor.cpp
  qf_editor.cpp speakermonitor.cpp TabAuto.cpp TabBase.cpp TabClass.cpp TabClub.cpp
  TabCompetition.cpp TabControl.cpp TabCourse.cpp TabList.cpp TabMulti.cpp TabRunner.cpp
  TabSI.cpp TabSpeaker.cpp TabTeam.cpp testmeos.cpp tests.cpp)

# MySQL, REST server, HTTP and online services (stage 2 on Linux).
set(MEOS_NET_SOURCES
  download.cpp infoserver.cpp MeosSQL.cpp mysqldaemon.cpp mysqlwrapper.cpp onlineinput.cpp
  onlineresults.cpp printresultservice.cpp restserver.cpp RestService.cpp socket.cpp)

# GDI/Win32 bound code: GUI backend, application frame, map drawing, serial port, zip,
# printing. Split into portable parts and platform backends in stage 1 on Linux.
set(MEOS_WIN32_SOURCES
  animationdata.cpp autocomplete.cpp autotask.cpp gdioutput.cpp image.cpp maprenderer.cpp
  meos.cpp printer.cpp progress.cpp SportIdent.cpp StdAfx.cpp Table.cpp toolbar.cpp zip.cpp)

meos_check_vcxproj_sync("${CMAKE_CURRENT_SOURCE_DIR}/MeOS.vcxproj"
  ${MEOS_CORE_SOURCES} ${MEOS_UI_SOURCES} ${MEOS_NET_SOURCES} ${MEOS_WIN32_SOURCES})

# ---------------------------------------------------------------------------
# Configuration settings (mirror MeOS.vcxproj)
# ---------------------------------------------------------------------------

# Release keeps debug information, as MeOS.vcxproj does.
if(MSVC)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,Release>:ProgramDatabase>")
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF")
else()
  set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g -DNDEBUG")
  check_cxx_compiler_flag(-Wchanges-meaning MEOS_HAS_WCHANGES_MEANING)
endif()

check_ipo_supported(RESULT MEOS_IPO_SUPPORTED LANGUAGES CXX)

if(NOT MSVC)
  set(MEOS_CASE_ALIAS_DIR "${CMAKE_CURRENT_BINARY_DIR}/case-aliases")
  file(GLOB meos_include_scan_files
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
  meos_generate_case_aliases("${MEOS_CASE_ALIAS_DIR}"
    SOURCES ${meos_include_scan_files}
    SEARCH_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                "${CMAKE_CURRENT_SOURCE_DIR}/libharu"
                "${CMAKE_CURRENT_SOURCE_DIR}/platform/compat/win32")
endif()

function(meos_apply_settings target)
  target_compile_features(${target} PUBLIC cxx_std_17)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(${target} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/libharu")

  # CMake does not define _DEBUG; MeOS has debug-only code guarded by it.
  target_compile_definitions(${target} PUBLIC "$<$<CONFIG:Debug>:_DEBUG>")

  if(MSVC)
    target_compile_definitions(${target} PUBLIC _WINDOWS UNICODE _UNICODE)
    target_compile_options(${target} PRIVATE /W3 /wd4267 /wd4244 /wd4018 /MP)
  else()
    target_include_directories(${target} PUBLIC
      "${MEOS_CASE_ALIAS_DIR}"
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/compat/win32")
    # MSVC stores narrow string literals in the ANSI code page (1252), and MeOS converts
    # narrow strings with code page 1252 explicitly. Using the same execution character
    # set keeps narrow strings byte-identical to the Windows build.
    target_compile_options(${target} PUBLIC -fexec-charset=CP1252)
    # Falling off the end of a non-void function is undefined behaviour that GCC and Clang
    # optimize on, unlike MSVC. Treat it as an error so that new cases are caught.
    target_compile_options(${target} PUBLIC -Werror=return-type)
    # MSVC accepts a member that reuses a name used earlier in the class with another
    # meaning (e.g. set in MachineContainer); GCC rejects that by default.
    if(MEOS_HAS_WCHANGES_MEANING)
      target_compile_options(${target} PUBLIC -Wno-changes-meaning)
    endif()
    if(MEOS_SANITIZE)
      target_compile_options(${target} PUBLIC
        "$<$<CONFIG:Debug>:-fsanitize=address,undefined;-fno-omit-frame-pointer>")
      target_link_options(${target} PUBLIC "$<$<CONFIG:Debug>:-fsanitize=address,undefined>")
    endif()
  endif()

  if(MEOS_IPO_SUPPORTED)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

add_library(meos_core OBJECT ${MEOS_CORE_SOURCES})
meos_apply_settings(meos_core)

if(NOT WIN32)
  # The subset of the Win32 API used by the portable code, implemented with POSIX means.
  add_library(meos_platform OBJECT
    platform/posix/win32_files.cpp
    platform/posix/win32_strings.cpp
    platform/posix/win32_time.cpp)
  meos_apply_settings(meos_platform)

  # Self test of the compatibility layer (run with ctest).
  add_executable(meos_platform_selftest platform/posix/tests/platform_selftest.cpp)
  meos_apply_settings(meos_platform_selftest)
  target_link_libraries(meos_platform_selftest PRIVATE meos_platform)
  add_test(NAME platform_selftest COMMAND meos_platform_selftest)

  # Compile-time checks that binary file structures keep their Windows layout.
  add_library(meos_layout_check OBJECT platform/posix/tests/layout_check.cpp)
  meos_apply_settings(meos_layout_check)
endif()

if(WIN32)
  enable_language(RC)

  configure_file(meos_dpi_manifest.xml "${CMAKE_CURRENT_BINARY_DIR}/meos_dpi.manifest" COPYONLY)

  add_executable(meos WIN32
    ${MEOS_UI_SOURCES} ${MEOS_NET_SOURCES} ${MEOS_WIN32_SOURCES}
    meos.rc meoslang.rc "${CMAKE_CURRENT_BINARY_DIR}/meos_dpi.manifest")
  meos_apply_settings(meos)
  set_target_properties(meos PROPERTIES OUTPUT_NAME MeOS)

  target_link_directories(meos PRIVATE
    "$<IF:$<CONFIG:Debug>,${CMAKE_CURRENT_SOURCE_DIR}/lib64_db,${CMAKE_CURRENT_SOURCE_DIR}/lib64>")
  target_link_libraries(meos PRIVATE
    meos_core
    Msimg32 comctl32 odbc32 odbccp32 winmm ws2_32 wininet
    zlibstat libharu RestBed libpng libmysql)

  # Runtime DLLs next to MeOS.exe. Debug then overwrites them with the debug builds
  # from dll_debug; in Release the second command copies the release DLLs again.
  file(GLOB meos_release_dlls "${CMAKE_CURRENT_SOURCE_DIR}/dll64/*.dll")
  file(GLOB meos_debug_dlls "${CMAKE_CURRENT_SOURCE_DIR}/dll_debug/*.dll")
  add_custom_command(TARGET meos POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${meos_release_dlls} "$<TARGET_FILE_DIR:meos>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${meos_debug_dlls},${meos_release_dlls}>" "$<TARGET_FILE_DIR:meos>"
    COMMAND_EXPAND_LISTS
    VERBATIM)
endif()
