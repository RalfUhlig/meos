# Embeds the resources of Windows resource scripts (.rc) as Qt resources, so that
# FindResource and LoadResource work on Linux (platform/qt/win32_resources.cpp).
#
# Single-line statements of the form
#     <name> <type> [load/memory attributes] "<file>"
# are read (images, language tables, sounds, HTML), and ACCELERATORS and DIALOG
# blocks (see below). Menus and version information are skipped. Symbols are resolved with
# the #define lines of the given headers and of the scripts themselves, as the
# preprocessor of the resource compiler does. New resources of an upstream drop
# therefore need no manual work.
#
# Every resource becomes the Qt resource ":/meos/resources/<type>/<name>":
#   - <type> is the number of a predefined type (BITMAP = 2, ICON = 14, HTML = 23,
#     RCDATA = 10, ...), a numeric type as written, or the type name in upper case
#     (PNG, WAVE);
#   - <name> is the resolved number or the name in upper case.
# The file content is embedded unchanged; win32_resources.cpp removes the
# BITMAPFILEHEADER of bitmaps as the resource compiler does.
#
# An ACCELERATORS block becomes the resource ":/meos/resources/9/<name>" (9 is
# RT_ACCELERATOR) as a text file with one entry per line, "<flags> <key> <id>" in
# decimal, which platform/qt/win32_app.cpp reads. The flag values are those of
# ACCEL (FVIRTKEY 1, FNOINVERT 2, FSHIFT 4, FCONTROL 8, FALT 16); the binary
# resource format of the resource compiler is not produced here, because nothing
# but this layer reads it. An entry that cannot be read stops the configure step,
# so that new accelerators of an upstream drop are noticed.
#
# A DIALOG or DIALOGEX block becomes ":/meos/resources/5/<name>" (5 is RT_DIALOG)
# as a text file with the lines "size <x> <y> <cx> <cy>" (in dialog units),
# "style <token> ..." and "caption <text>", which platform/qt/win32_dialogs.cpp
# reads. MeOS has a single dialog template, the splash screen, and it has no
# controls; a block with a control stops the configure step, because the layer
# builds no controls from a template (plans/linux-port-1.3-app-frame.md, E8).
#
# meos_generate_win32_resources(<output .qrc>
#                               RC_FILES <resource scripts>
#                               HEADERS <headers with #define lines>)

# One entry of an ACCELERATORS block: "<key>", <command>, <flags> or
# <virtual key>, <command>, VIRTKEY, <flags>. Sets <out> to "<flags> <key> <id>",
# or to an empty string for a line without an entry (a comment).
macro(meos_parse_accelerator line rc_file out)
  set(${out} "")
  string(REGEX REPLACE "//.*$" "" _accel_line "${line}")
  string(REGEX REPLACE "[ \t]+" " " _accel_line "${_accel_line}")
  string(STRIP "${_accel_line}" _accel_line)
  if(_accel_line)
    string(REPLACE "," ";" _accel_parts "${_accel_line}")
    list(LENGTH _accel_parts _accel_count)
    if(_accel_count LESS 2)
      message(FATAL_ERROR "${rc_file}: cannot read the accelerator entry: ${line}")
    endif()
    list(GET _accel_parts 0 _accel_key)
    list(GET _accel_parts 1 _accel_id)
    string(STRIP "${_accel_key}" _accel_key)
    string(STRIP "${_accel_id}" _accel_id)

    # The key: a character in quotes, ^<letter> for a control character, a number
    # or a virtual key.
    set(_accel_code "")
    if(_accel_key MATCHES "^\"\\^(.)\"$")
      string(TOUPPER "${CMAKE_MATCH_1}" _accel_letter)
      if(DEFINED _meos_char_code_${_accel_letter})
        math(EXPR _accel_code "${_meos_char_code_${_accel_letter}} - 64")
      endif()
    elseif(_accel_key MATCHES "^\"(.)\"$")
      if(DEFINED _meos_char_code_${CMAKE_MATCH_1})
        set(_accel_code "${_meos_char_code_${CMAKE_MATCH_1}}")
      endif()
    elseif(_accel_key MATCHES "^[0-9]+$")
      set(_accel_code "${_accel_key}")
    elseif(DEFINED _meos_vk_${_accel_key})
      set(_accel_code "${_meos_vk_${_accel_key}}")
    endif()
    if(NOT _accel_code)
      message(FATAL_ERROR "${rc_file}: unknown accelerator key ${_accel_key}: ${line}")
    endif()

    if(DEFINED _meos_symbol_${_accel_id})
      set(_accel_id "${_meos_symbol_${_accel_id}}")
    endif()
    if(NOT _accel_id MATCHES "^[0-9]+$")
      message(FATAL_ERROR "${rc_file}: unknown accelerator command ${_accel_id}: ${line}")
    endif()

    set(_accel_flags 0)
    foreach(_accel_index RANGE 2 ${_accel_count})
      if(_accel_index GREATER_EQUAL _accel_count)
        break()
      endif()
      list(GET _accel_parts ${_accel_index} _accel_flag)
      string(STRIP "${_accel_flag}" _accel_flag)
      string(TOUPPER "${_accel_flag}" _accel_flag)
      if(_accel_flag STREQUAL "VIRTKEY")
        math(EXPR _accel_flags "${_accel_flags} + 1")
      elseif(_accel_flag STREQUAL "NOINVERT")
        math(EXPR _accel_flags "${_accel_flags} + 2")
      elseif(_accel_flag STREQUAL "SHIFT")
        math(EXPR _accel_flags "${_accel_flags} + 4")
      elseif(_accel_flag STREQUAL "CONTROL")
        math(EXPR _accel_flags "${_accel_flags} + 8")
      elseif(_accel_flag STREQUAL "ALT")
        math(EXPR _accel_flags "${_accel_flags} + 16")
      elseif(NOT _accel_flag STREQUAL "ASCII")
        message(FATAL_ERROR "${rc_file}: unknown accelerator flag ${_accel_flag}: ${line}")
      endif()
    endforeach()
    set(${out} "${_accel_flags} ${_accel_code} ${_accel_id}")
  endif()
endmacro()

function(meos_generate_win32_resources qrc_file)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "RC_FILES;HEADERS")

  set(symbol_regex "^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+(0[xX][0-9a-fA-F]+|[0-9]+)[ \t]*$")
  foreach(file IN LISTS ARG_HEADERS ARG_RC_FILES)
    file(STRINGS "${file}" lines REGEX "^[ \t]*#[ \t]*define[ \t]")
    foreach(line IN LISTS lines)
      string(REPLACE "\r" "" line "${line}")
      if(line MATCHES "${symbol_regex}")
        math(EXPR value "${CMAKE_MATCH_2}")
        set(_meos_symbol_${CMAKE_MATCH_1} "${value}")
      endif()
    endforeach()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  endforeach()

  # Predefined resource types (RT_*) of the statements that name a file.
  set(_meos_type_CURSOR 12)
  set(_meos_type_BITMAP 2)
  set(_meos_type_ICON 14)
  set(_meos_type_FONT 8)
  set(_meos_type_RCDATA 10)
  set(_meos_type_MESSAGETABLE 11)
  set(_meos_type_HTML 23)
  set(_meos_type_MANIFEST 24)

  # Virtual keys of accelerator entries (VIRTKEY). MeOS uses only character keys;
  # the list grows with what an upstream drop uses.
  set(_meos_vk_VK_F1 112)
  set(_meos_vk_VK_F2 113)
  set(_meos_vk_VK_F3 114)
  set(_meos_vk_VK_F4 115)
  set(_meos_vk_VK_F5 116)
  set(_meos_vk_VK_F6 117)
  set(_meos_vk_VK_F7 118)
  set(_meos_vk_VK_F8 119)
  set(_meos_vk_VK_F9 120)
  set(_meos_vk_VK_F10 121)
  set(_meos_vk_VK_F11 122)
  set(_meos_vk_VK_F12 123)
  set(_meos_vk_VK_INSERT 45)
  set(_meos_vk_VK_DELETE 46)
  set(_meos_vk_VK_RETURN 13)
  set(_meos_vk_VK_ESCAPE 27)

  set(attribute "[ \t]+(PRELOAD|LOADONCALL|FIXED|MOVEABLE|DISCARDABLE|PURE|IMPURE)")
  set(resource_regex "^[ \t]*([A-Za-z0-9_]+)[ \t]+([A-Za-z0-9_]+)(${attribute})*[ \t]+\"([^\"]+)\"[ \t]*$")

  # Character codes of the printable ASCII range, for accelerator keys.
  foreach(code RANGE 32 126)
    string(ASCII ${code} character)
    set(_meos_char_code_${character} ${code})
  endforeach()

  get_filename_component(generated_dir "${qrc_file}" DIRECTORY)

  set(entries "")
  set(keys "")
  foreach(rc_file IN LISTS ARG_RC_FILES)
    get_filename_component(rc_dir "${rc_file}" DIRECTORY)
    file(STRINGS "${rc_file}" lines)
    set(accelerator_name "")
    set(dialog_name "")
    foreach(line IN LISTS lines)
      string(REPLACE "\r" "" line "${line}")

      # A DIALOG block: size, style and caption until END, which must be empty.
      if(dialog_name)
        if(line MATCHES "^[ \t]*(BEGIN|\\{)[ \t]*$")
          set(dialog_in_body TRUE)
          continue()
        elseif(line MATCHES "^[ \t]*(END|\\})[ \t]*$")
          set(dialog_file "${generated_dir}/meos_dialog_${dialog_name}.txt")
          file(WRITE "${dialog_file}.tmp" "${dialog_content}")
          configure_file("${dialog_file}.tmp" "${dialog_file}" COPYONLY)
          file(REMOVE "${dialog_file}.tmp")
          set(key "5/${dialog_name}")
          if(key IN_LIST keys)
            message(FATAL_ERROR "${rc_file}: duplicate resource ${key}")
          endif()
          list(APPEND keys "${key}")
          string(APPEND entries "    <file alias=\"${key}\">${dialog_file}</file>\n")
          set(dialog_name "")
          continue()
        endif()
        string(STRIP "${line}" dialog_line)
        if(dialog_in_body)
          if(dialog_line)
            message(FATAL_ERROR "${rc_file}: the dialog template ${dialog_name} has controls, "
                                "which this layer does not build: ${line}")
          endif()
        elseif(dialog_line MATCHES "^STYLE[ \t]+(.*)$")
          string(REGEX REPLACE "[ \t]*\\|[ \t]*" " " dialog_style "${CMAKE_MATCH_1}")
          string(APPEND dialog_content "style ${dialog_style}\n")
        elseif(dialog_line MATCHES "^CAPTION[ \t]+\"(.*)\"$")
          string(APPEND dialog_content "caption ${CMAKE_MATCH_1}\n")
        endif()
        continue()
      endif()
      if(line MATCHES "^[ \t]*([A-Za-z0-9_]+)[ \t]+DIALOG(EX)?[ \t]+([0-9]+)[ \t]*,[ \t]*([0-9]+)[ \t]*,[ \t]*([0-9]+)[ \t]*,[ \t]*([0-9]+)")
        set(dialog_name "${CMAKE_MATCH_1}")
        if(DEFINED _meos_symbol_${dialog_name})
          set(dialog_name "${_meos_symbol_${dialog_name}}")
        else()
          string(TOUPPER "${dialog_name}" dialog_name)
        endif()
        set(dialog_content "size ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5} ${CMAKE_MATCH_6}\n")
        set(dialog_in_body FALSE)
        continue()
      endif()

      # An ACCELERATORS block: one entry per line until END.
      if(accelerator_name)
        if(line MATCHES "^[ \t]*(BEGIN|\\{)[ \t]*$")
          continue()
        elseif(line MATCHES "^[ \t]*(END|\\})[ \t]*$")
          set(accelerator_file "${generated_dir}/meos_accelerators_${accelerator_name}.txt")
          file(WRITE "${accelerator_file}.tmp" "${accelerator_entries}")
          configure_file("${accelerator_file}.tmp" "${accelerator_file}" COPYONLY)
          file(REMOVE "${accelerator_file}.tmp")
          set(key "9/${accelerator_name}")
          if(key IN_LIST keys)
            message(FATAL_ERROR "${rc_file}: duplicate resource ${key}")
          endif()
          list(APPEND keys "${key}")
          string(APPEND entries "    <file alias=\"${key}\">${accelerator_file}</file>\n")
          set(accelerator_name "")
          continue()
        endif()
        meos_parse_accelerator("${line}" "${rc_file}" entry)
        if(entry)
          string(APPEND accelerator_entries "${entry}\n")
        endif()
        continue()
      endif()
      if(line MATCHES "^[ \t]*([A-Za-z0-9_]+)[ \t]+ACCELERATORS[ \t]*$")
        set(accelerator_name "${CMAKE_MATCH_1}")
        if(DEFINED _meos_symbol_${accelerator_name})
          set(accelerator_name "${_meos_symbol_${accelerator_name}}")
        else()
          string(TOUPPER "${accelerator_name}" accelerator_name)
        endif()
        set(accelerator_entries "")
        continue()
      endif()

      if(NOT line MATCHES "${resource_regex}")
        continue()
      endif()
      set(name "${CMAKE_MATCH_1}")
      set(type "${CMAKE_MATCH_2}")
      set(path "${CMAKE_MATCH_5}")

      foreach(token name type)
        if(DEFINED _meos_symbol_${${token}})
          set(${token} "${_meos_symbol_${${token}}}")
        endif()
      endforeach()
      string(TOUPPER "${type}" type_upper)
      if(DEFINED _meos_type_${type_upper})
        set(type "${_meos_type_${type_upper}}")
      elseif(NOT type MATCHES "^[0-9]+$")
        set(type "${type_upper}")
      endif()
      if(NOT name MATCHES "^[0-9]+$")
        string(TOUPPER "${name}" name)
      endif()

      # The resource compiler finds files regardless of letter case (meos.ICO).
      string(REPLACE "\\" "/" path "${path}")
      get_filename_component(path "${path}" ABSOLUTE BASE_DIR "${rc_dir}")
      if(NOT EXISTS "${path}")
        get_filename_component(file_dir "${path}" DIRECTORY)
        get_filename_component(file_name "${path}" NAME)
        string(TOLOWER "${file_name}" wanted)
        file(GLOB candidates RELATIVE "${file_dir}" "${file_dir}/*")
        set(found "")
        foreach(candidate IN LISTS candidates)
          string(TOLOWER "${candidate}" lower)
          if(lower STREQUAL wanted)
            set(found "${file_dir}/${candidate}")
          endif()
        endforeach()
        if(NOT found)
          message(FATAL_ERROR "${rc_file}: resource file not found: ${path}")
        endif()
        set(path "${found}")
      endif()

      set(key "${type}/${name}")
      if(key IN_LIST keys)
        message(FATAL_ERROR "${rc_file}: duplicate resource ${key}")
      endif()
      list(APPEND keys "${key}")
      string(REPLACE "&" "&amp;" xml_path "${path}")
      string(APPEND entries "    <file alias=\"${key}\">${xml_path}</file>\n")
    endforeach()
  endforeach()

  set(content "<!DOCTYPE RCC>\n<!-- Generated by cmake/Win32Resources.cmake. -->\n<RCC version=\"1.0\">\n")
  string(APPEND content "  <qresource prefix=\"/meos/resources\">\n${entries}  </qresource>\n</RCC>\n")
  # Rewritten only on change, so that the resources are not rebuilt on every run.
  file(WRITE "${qrc_file}.tmp" "${content}")
  configure_file("${qrc_file}.tmp" "${qrc_file}" COPYONLY)
  file(REMOVE "${qrc_file}.tmp")
endfunction()
