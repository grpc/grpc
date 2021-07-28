# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


macro(_cmake_find_compiler lang)
  # Use already-enabled languages for reference.
  get_property(_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
  list(REMOVE_ITEM _languages "${lang}")

  if(CMAKE_${lang}_COMPILER_INIT)
    # Search only for the specified compiler.
    set(CMAKE_${lang}_COMPILER_LIST "${CMAKE_${lang}_COMPILER_INIT}")
  else()
    # Re-order the compiler list with preferred vendors first.
    set(_${lang}_COMPILER_LIST "${CMAKE_${lang}_COMPILER_LIST}")
    set(CMAKE_${lang}_COMPILER_LIST "")
    # Prefer vendors of compilers from reference languages.
    foreach(l ${_languages})
      list(APPEND CMAKE_${lang}_COMPILER_LIST
        ${_${lang}_COMPILER_NAMES_${CMAKE_${l}_COMPILER_ID}})
    endforeach()
    # Prefer vendors based on the platform.
    list(APPEND CMAKE_${lang}_COMPILER_LIST ${CMAKE_${lang}_COMPILER_NAMES})
    # Append the rest of the list and remove duplicates.
    list(APPEND CMAKE_${lang}_COMPILER_LIST ${_${lang}_COMPILER_LIST})
    unset(_${lang}_COMPILER_LIST)
    list(REMOVE_DUPLICATES CMAKE_${lang}_COMPILER_LIST)
    if(CMAKE_${lang}_COMPILER_EXCLUDE)
      list(REMOVE_ITEM CMAKE_${lang}_COMPILER_LIST
        ${CMAKE_${lang}_COMPILER_EXCLUDE})
    endif()
  endif()

  # Look for directories containing compilers of reference languages.
  set(_${lang}_COMPILER_HINTS)
  foreach(l ${_languages})
    if(CMAKE_${l}_COMPILER AND IS_ABSOLUTE "${CMAKE_${l}_COMPILER}")
      get_filename_component(_hint "${CMAKE_${l}_COMPILER}" PATH)
      if(IS_DIRECTORY "${_hint}")
        list(APPEND _${lang}_COMPILER_HINTS "${_hint}")
      endif()
      unset(_hint)
    endif()
  endforeach()

  # Find the compiler.
  if(_${lang}_COMPILER_HINTS)
    # Prefer directories containing compilers of reference languages.
    list(REMOVE_DUPLICATES _${lang}_COMPILER_HINTS)
    find_program(CMAKE_${lang}_COMPILER
      NAMES ${CMAKE_${lang}_COMPILER_LIST}
      PATHS ${_${lang}_COMPILER_HINTS}
      NO_DEFAULT_PATH
      DOC "${lang} compiler")
  endif()
  if(CMAKE_HOST_WIN32 AND CMAKE_GENERATOR MATCHES "Ninja")
    # On Windows command-line builds, the Makefile generators each imply
    # a preferred compiler tool.  The Ninja generator does not imply a
    # compiler tool, so use the compiler that occurs first in PATH.
    find_program(CMAKE_${lang}_COMPILER
      NAMES ${CMAKE_${lang}_COMPILER_LIST}
      NAMES_PER_DIR
      DOC "${lang} compiler"
      NO_PACKAGE_ROOT_PATH
      NO_CMAKE_PATH
      NO_CMAKE_ENVIRONMENT_PATH
      NO_CMAKE_SYSTEM_PATH
      )
  endif()
  find_program(CMAKE_${lang}_COMPILER NAMES ${CMAKE_${lang}_COMPILER_LIST} DOC "${lang} compiler")
  if(CMAKE_${lang}_COMPILER_INIT AND NOT CMAKE_${lang}_COMPILER)
    set_property(CACHE CMAKE_${lang}_COMPILER PROPERTY VALUE "${CMAKE_${lang}_COMPILER_INIT}")
  endif()
  unset(_${lang}_COMPILER_HINTS)
  unset(_languages)

  # Look for a make tool provided by Xcode
  if(CMAKE_HOST_APPLE)
    macro(_query_xcrun compiler_name result_var_keyword result_var)
      if(NOT "x${result_var_keyword}" STREQUAL "xRESULT_VAR")
        message(FATAL_ERROR "Bad arguments to macro")
      endif()
      execute_process(COMMAND xcrun --find ${compiler_name}
        OUTPUT_VARIABLE _xcrun_out OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE _xcrun_err)
      set("${result_var}" "${_xcrun_out}")
    endmacro()

    set(xcrun_result)
    if (CMAKE_${lang}_COMPILER MATCHES "^/usr/bin/(.+)$")
      _query_xcrun("${CMAKE_MATCH_1}" RESULT_VAR xcrun_result)
    elseif (CMAKE_${lang}_COMPILER STREQUAL "CMAKE_${lang}_COMPILER-NOTFOUND")
      foreach(comp ${CMAKE_${lang}_COMPILER_LIST})
        _query_xcrun("${comp}" RESULT_VAR xcrun_result)
        if(xcrun_result)
          break()
        endif()
      endforeach()
    endif()
    if (xcrun_result)
      set_property(CACHE CMAKE_${lang}_COMPILER PROPERTY VALUE "${xcrun_result}")
    endif()
  endif()
endmacro()

macro(_cmake_find_compiler_path lang)
  if(CMAKE_${lang}_COMPILER)
    # we only get here if CMAKE_${lang}_COMPILER was specified using -D or a pre-made CMakeCache.txt
    # (e.g. via ctest) or set in CMAKE_TOOLCHAIN_FILE
    # if CMAKE_${lang}_COMPILER is a list, use the first item as
    # CMAKE_${lang}_COMPILER and the rest as CMAKE_${lang}_COMPILER_ARG1
    set(CMAKE_${lang}_COMPILER_ARG1 "${CMAKE_${lang}_COMPILER}")
    list(POP_FRONT CMAKE_${lang}_COMPILER_ARG1 CMAKE_${lang}_COMPILER)
    list(JOIN CMAKE_${lang}_COMPILER_ARG1 " " CMAKE_${lang}_COMPILER_ARG1)

    # find the compiler in the PATH if necessary
    # if compiler (and arguments) comes from cache then synchronize cache with updated CMAKE_<LANG>_COMPILER
    get_filename_component(_CMAKE_USER_${lang}_COMPILER_PATH "${CMAKE_${lang}_COMPILER}" PATH)
    if(NOT _CMAKE_USER_${lang}_COMPILER_PATH)
      find_program(CMAKE_${lang}_COMPILER_WITH_PATH NAMES ${CMAKE_${lang}_COMPILER})
      if(CMAKE_${lang}_COMPILER_WITH_PATH)
        set(CMAKE_${lang}_COMPILER ${CMAKE_${lang}_COMPILER_WITH_PATH})
        get_property(_CMAKE_${lang}_COMPILER_CACHED CACHE CMAKE_${lang}_COMPILER PROPERTY TYPE)
        if(_CMAKE_${lang}_COMPILER_CACHED)
          set(CMAKE_${lang}_COMPILER "${CMAKE_${lang}_COMPILER}" CACHE STRING "${lang} compiler" FORCE)
        endif()
        unset(_CMAKE_${lang}_COMPILER_CACHED)
      endif()
      unset(CMAKE_${lang}_COMPILER_WITH_PATH CACHE)
    elseif (EXISTS ${CMAKE_${lang}_COMPILER})
      get_property(_CMAKE_${lang}_COMPILER_CACHED CACHE CMAKE_${lang}_COMPILER PROPERTY TYPE)
      if(_CMAKE_${lang}_COMPILER_CACHED)
        set(CMAKE_${lang}_COMPILER "${CMAKE_${lang}_COMPILER}" CACHE STRING "${lang} compiler" FORCE)
      endif()
      unset(_CMAKE_${lang}_COMPILER_CACHED)
    endif()
  endif()
endmacro()

function(_cmake_find_compiler_sysroot lang)
  if(CMAKE_${lang}_COMPILER_ID STREQUAL "GNU")
    execute_process(COMMAND "${CMAKE_${lang}_COMPILER}" -print-sysroot
      OUTPUT_STRIP_TRAILING_WHITESPACE
      OUTPUT_VARIABLE _cmake_sysroot_run_out
      ERROR_VARIABLE _cmake_sysroot_run_err)

    if(_cmake_sysroot_run_out AND NOT _cmake_sysroot_run_err
        AND NOT _cmake_sysroot_run_out STREQUAL "/"
        AND IS_DIRECTORY "${_cmake_sysroot_run_out}/usr")
      file(TO_CMAKE_PATH "${_cmake_sysroot_run_out}/usr" _cmake_sysroot_run_out_usr)
      set(CMAKE_${lang}_COMPILER_SYSROOT "${_cmake_sysroot_run_out_usr}" PARENT_SCOPE)
    else()
      set(CMAKE_${lang}_COMPILER_SYSROOT "" PARENT_SCOPE)
    endif()
  endif()
endfunction()
