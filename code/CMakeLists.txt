# MeOS build definition for Windows (MSVC) and Linux (GCC/Clang).
#
# code/MeOS.vcxproj remains a second build path on Windows. cmake/VcxprojSync.cmake
# fails the configure step if both definitions list different source files.
#
# Linux port: every source except meos.cpp (the application frame, stage 1.3) is built
# and linked on Linux against the Win32 subset in platform/, see plans/linux-port.md.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CaseAliases)
include(CheckCXXCompilerFlag)
include(CheckIPOSupported)
include(VcxprojSync)
include(Win32Resources)
if(NOT WIN32)
  include(RestBed)
endif()

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

# MeOS is built for x64: the prebuilt libraries in code/lib64 and code/lib64_db are
# x64, and the Ninja presets cannot set the target platform themselves - the compiler
# comes from the environment of the Developer shell. Stop here instead of letting the
# linker fail with LNK4272 and every symbol of the prebuilt libraries unresolved.
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "MeOS builds for x64, but ${CMAKE_CXX_COMPILER} targets ${CMAKE_SIZEOF_VOID_P}-byte pointers.\n"
    "Configure from an x64 Developer shell: \"x64 Native Tools Command Prompt for VS 2022\", or run "
    "vcvarsall.bat x64, or in PowerShell Enter-VsDevShell -Arch amd64 -HostArch amd64. "
    "Delete the build directory afterwards, because the compiler check is cached.")
endif()

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
                "${CMAKE_CURRENT_SOURCE_DIR}/platform/compat/win32")
endif()

function(meos_apply_settings target)
  target_compile_features(${target} PUBLIC cxx_std_17)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

  # CMake does not define _DEBUG; MeOS has debug-only code guarded by it.
  target_compile_definitions(${target} PUBLIC "$<$<CONFIG:Debug>:_DEBUG>")

  if(WIN32)
    # The prebuilt libharu. Elsewhere hpdf.h comes from the system package.
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libharu")
  endif()

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
      # Without the alignment check: oDataContainer keeps int fields at unaligned offsets
      # of byte buffers, as on Windows; x86-64 and AArch64 read them correctly.
      target_compile_options(${target} PUBLIC
        "$<$<CONFIG:Debug>:-fsanitize=address,undefined;-fno-sanitize=alignment;-fno-omit-frame-pointer>")
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
  find_package(Threads REQUIRED)

  # The subset of the Win32 API used by the portable code, implemented with POSIX means.
  add_library(meos_platform OBJECT
    platform/posix/minizip_iowin32.cpp
    platform/posix/win32_files.cpp
    platform/posix/win32_handle.cpp
    platform/posix/win32_inet.cpp
    platform/posix/win32_network.cpp
    platform/posix/win32_serial.cpp
    platform/posix/win32_strings.cpp
    platform/posix/win32_threads.cpp
    platform/posix/win32_time.cpp)
  meos_apply_settings(meos_platform)
  target_link_libraries(meos_platform PUBLIC Threads::Threads)

  # System libraries in place of the prebuilt ones in lib64 (docs/BUILD_LINUX.md).
  find_package(PkgConfig REQUIRED)
  find_package(PNG REQUIRED)
  find_package(ZLIB REQUIRED)
  pkg_check_modules(MEOS_MINIZIP REQUIRED IMPORTED_TARGET minizip)
  # The MariaDB client, or the MySQL client library in its place. The code includes the
  # MySQL headers shipped in code/mysql ("mysql/mysql.h" relative to mysqlwrapper.h);
  # mysql_api_check verifies that they match the header of the library linked here.
  pkg_search_module(MEOS_MYSQL REQUIRED IMPORTED_TARGET libmariadb mysqlclient)
  find_library(MEOS_HPDF_LIBRARY hpdf REQUIRED)
  find_path(MEOS_HPDF_INCLUDE_DIR hpdf.h REQUIRED)
  add_library(meos_hpdf INTERFACE)
  target_include_directories(meos_hpdf INTERFACE "${MEOS_HPDF_INCLUDE_DIR}")
  target_link_libraries(meos_hpdf INTERFACE "${MEOS_HPDF_LIBRARY}")
  target_link_libraries(meos_core PUBLIC meos_hpdf)
  # Explicit instantiations that optimized (LTO) builds need, see the file.
  target_sources(meos_core PRIVATE platform/posix/intkeymap_instances.cpp)

  # The SI reader (stage 1.1).
  add_library(meos_si OBJECT SportIdent.cpp)
  meos_apply_settings(meos_si)

  # The GDI/window based sources, the tabs and the rest of the Win32 group (stage 1.2),
  # built against the Win32 declarations in platform/compat/win32. meos.cpp follows in
  # stage 1.3.
  set(MEOS_LINUX_GUI_SOURCES ${MEOS_WIN32_SOURCES})
  list(REMOVE_ITEM MEOS_LINUX_GUI_SOURCES meos.cpp SportIdent.cpp)
  add_library(meos_gui OBJECT ${MEOS_LINUX_GUI_SOURCES} ${MEOS_UI_SOURCES})
  meos_apply_settings(meos_gui)
  target_link_libraries(meos_gui PUBLIC PNG::PNG ZLIB::ZLIB PkgConfig::MEOS_MINIZIP)

  # MySQL, REST server, HTTP and online services. They link since stage 1.2 (the tabs
  # need them); WinInet reports no connection until stage 2.
  meos_add_restbed(meos_restbed)
  add_library(meos_net OBJECT ${MEOS_NET_SOURCES})
  meos_apply_settings(meos_net)
  target_link_libraries(meos_net PUBLIC meos_restbed PkgConfig::MEOS_MYSQL)

  # The Win32 window manager subset (USER32) on Qt 6 widgets (stage 1.2).
  find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
  # Selawik, the substitute for Segoe UI (platform/qt/fonts/README.md).
  qt_add_resources(MEOS_FONT_RESOURCES platform/qt/fonts/meos_fonts.qrc)
  add_library(meos_win32ui OBJECT
    ${MEOS_FONT_RESOURCES}
    platform/qt/win32_app.cpp
    platform/qt/win32_canvas.cpp
    platform/qt/win32_clipboard.cpp
    platform/qt/win32_commctrl.cpp
    platform/qt/win32_controls.cpp
    platform/qt/win32_dialogs.cpp
    platform/qt/win32_gdi.cpp
    platform/qt/win32_message.cpp
    platform/qt/win32_resources.cpp
    platform/qt/win32_screen.cpp
    platform/qt/win32_sound.cpp
    platform/qt/win32_text.cpp
    platform/qt/win32_window.cpp)
  meos_apply_settings(meos_win32ui)
  target_link_libraries(meos_win32ui PUBLIC meos_platform Qt6::Widgets)

  # main() for programs that define WinMain.
  # The resources of meos.rc and meoslang.rc (images, language tables, sounds), found
  # by FindResource. Uncompressed, so that LockResource points into the program image.
  meos_generate_win32_resources("${CMAKE_CURRENT_BINARY_DIR}/meos_resources.qrc"
    RC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/meos.rc" "${CMAKE_CURRENT_SOURCE_DIR}/meoslang.rc"
    HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/resource.h")
  qt_add_resources(MEOS_WIN32_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/meos_resources.qrc" OPTIONS --no-compress)
  add_library(meos_resources OBJECT ${MEOS_WIN32_RESOURCES})
  meos_apply_settings(meos_resources)
  target_link_libraries(meos_resources PUBLIC Qt6::Core)

  add_library(meos_win32ui_main OBJECT platform/qt/win32_main.cpp)
  meos_apply_settings(meos_win32ui_main)
  target_link_libraries(meos_win32ui_main PUBLIC Qt6::Widgets)

  # The globals and helper functions of meos.cpp for programs without it, and a demo
  # competition (platform/qt/workbench/app_frame.h).
  add_library(meos_app_frame OBJECT
    platform/qt/workbench/app_frame.cpp
    platform/qt/workbench/demo_competition.cpp)
  meos_apply_settings(meos_app_frame)

  # Links all of MeOS except meos.cpp: object libraries do not pass their objects on,
  # so every program lists them.
  function(meos_link_meos target)
    target_link_libraries(${target} PRIVATE
      meos_core meos_gui meos_net meos_si meos_app_frame meos_win32ui meos_resources meos_platform
      meos_win32ui_main)
  endfunction()

  # GUI workbench (stage 1.2.6): real gdioutput pages on the Qt backend, for manual
  # checks and the comparison with Windows. Not part of a MeOS installation.
  add_executable(meos_gui_workbench platform/qt/workbench/workbench.cpp)
  meos_apply_settings(meos_gui_workbench)
  meos_link_meos(meos_gui_workbench)

  # Builds every page and writes its screenshot and layout (stage 1.2.7). The only
  # test that runs real gdioutput pages; the files are meant for the comparison with
  # a Windows run (docs/BUILD_LINUX.md).
  add_test(NAME workbench_shot
           COMMAND meos_gui_workbench -shot "${CMAKE_CURRENT_BINARY_DIR}/workbench_shot/linux")
  set_tests_properties(workbench_shot PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/workbench_shot_data"
    TIMEOUT 300)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/workbench_shot")

  # Load test of the core (stage 1.2.6): competition file round trip, results, result
  # list, ZIP archive. XDG_DATA_HOME keeps its settings inside the build tree.
  add_executable(meos_core_load_test platform/qt/tests/core_load_test.cpp)
  meos_apply_settings(meos_core_load_test)
  target_include_directories(meos_core_load_test PRIVATE platform/qt/workbench)
  meos_link_meos(meos_core_load_test)
  add_test(NAME core_load_test COMMAND meos_core_load_test)
  set_tests_properties(core_load_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/core_load_test_data"
    TIMEOUT 300)

  # The MySQL headers in code/mysql (used by mysqlwrapper.cpp) match those of the linked
  # client library. mysql_api_vendored.cpp finds "mysql/mysql.h" in code/, the system
  # header comes from the include directories of the pkg-config module.
  add_executable(meos_mysql_api_check
    platform/posix/tests/mysql_api_check.cpp
    platform/posix/tests/mysql_api_system.cpp
    platform/posix/tests/mysql_api_vendored.cpp)
  meos_apply_settings(meos_mysql_api_check)
  target_link_libraries(meos_mysql_api_check PRIVATE PkgConfig::MEOS_MYSQL)
  add_test(NAME mysql_api_check COMMAND meos_mysql_api_check)

  # Self test of the compatibility layer (run with ctest).
  add_executable(meos_platform_selftest platform/posix/tests/platform_selftest.cpp)
  meos_apply_settings(meos_platform_selftest)
  target_link_libraries(meos_platform_selftest PRIVATE meos_platform)
  add_test(NAME platform_selftest COMMAND meos_platform_selftest)

  add_executable(meos_win32ui_selftest platform/qt/tests/win32ui_selftest.cpp)
  meos_apply_settings(meos_win32ui_selftest)
  # Object libraries do not pass their objects on, so both are linked here.
  target_link_libraries(meos_win32ui_selftest PRIVATE meos_win32ui meos_platform Qt6::Widgets)
  add_test(NAME win32ui_selftest COMMAND meos_win32ui_selftest)
  set_tests_properties(win32ui_selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(meos_controls_selftest platform/qt/tests/controls_selftest.cpp)
  meos_apply_settings(meos_controls_selftest)
  target_link_libraries(meos_controls_selftest PRIVATE meos_win32ui meos_platform Qt6::Widgets)
  add_test(NAME controls_selftest COMMAND meos_controls_selftest)
  set_tests_properties(controls_selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(meos_gdi_selftest platform/qt/tests/gdi_selftest.cpp)
  meos_apply_settings(meos_gdi_selftest)
  target_link_libraries(meos_gdi_selftest PRIVATE meos_win32ui meos_platform Qt6::Widgets)
  add_test(NAME gdi_selftest COMMAND meos_gdi_selftest)
  set_tests_properties(gdi_selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(meos_dialogs_selftest platform/qt/tests/dialogs_selftest.cpp)
  meos_apply_settings(meos_dialogs_selftest)
  target_link_libraries(meos_dialogs_selftest PRIVATE meos_win32ui meos_resources meos_platform Qt6::Widgets)
  add_test(NAME dialogs_selftest COMMAND meos_dialogs_selftest "${CMAKE_CURRENT_SOURCE_DIR}")
  # Modal dialogs that do not close would hang the test.
  set_tests_properties(dialogs_selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

  # Text metrics of the Qt backend, compared with those of Windows GDI once
  # gdi_metrics_windows.csv has been created on Windows (see gdi_metrics.cpp).
  add_executable(meos_gdi_metrics platform/qt/tests/gdi_metrics.cpp)
  meos_apply_settings(meos_gdi_metrics)
  target_link_libraries(meos_gdi_metrics PRIVATE meos_win32ui meos_win32ui_main meos_platform Qt6::Widgets)
  add_test(NAME gdi_metrics COMMAND meos_gdi_metrics "${CMAKE_CURRENT_BINARY_DIR}/gdi_metrics_linux.csv")
  set_tests_properties(gdi_metrics PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                       FIXTURES_SETUP gdi_metrics_csv)

  add_executable(meos_gdi_metrics_compare platform/qt/tests/gdi_metrics_compare.cpp)
  meos_apply_settings(meos_gdi_metrics_compare)
  add_test(NAME gdi_metrics_compare
           COMMAND meos_gdi_metrics_compare "${CMAKE_CURRENT_BINARY_DIR}/gdi_metrics_linux.csv"
                   "${CMAKE_CURRENT_SOURCE_DIR}/platform/qt/tests/gdi_metrics_windows.csv" "Arial" "Times New Roman"
                   "Lucida Console")
  set_tests_properties(gdi_metrics_compare PROPERTIES FIXTURES_REQUIRED gdi_metrics_csv SKIP_RETURN_CODE 77)

  # Compile-time checks that binary file structures keep their Windows layout.
  add_library(meos_layout_check OBJECT platform/posix/tests/layout_check.cpp)
  meos_apply_settings(meos_layout_check)
endif()

if(WIN32)
  enable_language(RC)

  configure_file(meos_dpi_manifest.xml "${CMAKE_CURRENT_BINARY_DIR}/meos_dpi.manifest" COPYONLY)
  # The workbench keeps the common controls but declares itself DPI unaware, so that its
  # pages are the 96 dpi layout on every screen (platform/qt/workbench/workbench.manifest).
  configure_file(platform/qt/workbench/workbench.manifest
                 "${CMAKE_CURRENT_BINARY_DIR}/workbench.manifest" COPYONLY)

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

  # Runtime DLLs next to the program. Debug then overwrites them with the debug builds
  # from dll_debug; in Release the second command copies the release DLLs again.
  file(GLOB meos_release_dlls "${CMAKE_CURRENT_SOURCE_DIR}/dll64/*.dll")
  file(GLOB meos_debug_dlls "${CMAKE_CURRENT_SOURCE_DIR}/dll_debug/*.dll")
  function(meos_copy_runtime_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${meos_release_dlls} "$<TARGET_FILE_DIR:${target}>"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "$<IF:$<CONFIG:Debug>,${meos_debug_dlls},${meos_release_dlls}>" "$<TARGET_FILE_DIR:${target}>"
      COMMAND_EXPAND_LISTS
      VERBATIM)
  endfunction()
  meos_copy_runtime_dlls(meos)

  # The GUI workbench with MSVC (stage 1.2.7): the same program as on Linux, so that
  # the pages can be compared side by side (platform/qt/workbench/workbench.cpp, which
  # uses the Win32 API and gdioutput only). It replaces meos.cpp by app_frame.cpp.
  # Not part of a MeOS installation.
  set(MEOS_WORKBENCH_MEOS_SOURCES ${MEOS_WIN32_SOURCES})
  list(REMOVE_ITEM MEOS_WORKBENCH_MEOS_SOURCES meos.cpp)
  add_executable(meos_gui_workbench WIN32
    ${MEOS_UI_SOURCES} ${MEOS_NET_SOURCES} ${MEOS_WORKBENCH_MEOS_SOURCES}
    platform/qt/workbench/app_frame.cpp
    platform/qt/workbench/demo_competition.cpp
    platform/qt/workbench/workbench.cpp
    meos.rc meoslang.rc "${CMAKE_CURRENT_BINARY_DIR}/workbench.manifest")
  meos_apply_settings(meos_gui_workbench)
  target_include_directories(meos_gui_workbench PRIVATE platform/qt/workbench)
  target_link_directories(meos_gui_workbench PRIVATE
    "$<IF:$<CONFIG:Debug>,${CMAKE_CURRENT_SOURCE_DIR}/lib64_db,${CMAKE_CURRENT_SOURCE_DIR}/lib64>")
  target_link_libraries(meos_gui_workbench PRIVATE
    meos_core
    Msimg32 comctl32 odbc32 odbccp32 winmm ws2_32 wininet
    zlibstat libharu RestBed libpng libmysql)
  meos_copy_runtime_dlls(meos_gui_workbench)

  # The text metrics of real GDI, the reference for the Qt backend. The test compares
  # a fresh measurement with the checked in one (platform/qt/tests/gdi_metrics.cpp).
  add_executable(meos_gdi_metrics WIN32 platform/qt/tests/gdi_metrics.cpp)
  meos_apply_settings(meos_gdi_metrics)
  target_link_libraries(meos_gdi_metrics PRIVATE user32 gdi32)
  add_test(NAME gdi_metrics COMMAND meos_gdi_metrics "${CMAKE_CURRENT_BINARY_DIR}/gdi_metrics_run.csv")
  set_tests_properties(gdi_metrics PROPERTIES FIXTURES_SETUP gdi_metrics_csv)

  add_executable(meos_gdi_metrics_compare platform/qt/tests/gdi_metrics_compare.cpp)
  meos_apply_settings(meos_gdi_metrics_compare)
  add_test(NAME gdi_metrics_compare
           COMMAND meos_gdi_metrics_compare "${CMAKE_CURRENT_BINARY_DIR}/gdi_metrics_run.csv"
                   "${CMAKE_CURRENT_SOURCE_DIR}/platform/qt/tests/gdi_metrics_windows.csv" "Arial" "Times New Roman"
                   "Lucida Console" "Segoe UI")
  set_tests_properties(gdi_metrics_compare PROPERTIES FIXTURES_REQUIRED gdi_metrics_csv SKIP_RETURN_CODE 77)
endif()
