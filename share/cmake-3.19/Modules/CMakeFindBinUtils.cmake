# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# search for additional tools required for C/C++ (and other languages ?)
#
# If the internal cmake variable _CMAKE_TOOLCHAIN_PREFIX is set, this is used
# as prefix for the tools (e.g. arm-elf-gcc etc.)
# If the cmake variable _CMAKE_TOOLCHAIN_LOCATION is set, the compiler is
# searched only there. The other tools are at first searched there, then
# also in the default locations.
#
# Sets the following variables:
#   CMAKE_AR
#   CMAKE_RANLIB
#   CMAKE_LINKER
#   CMAKE_MT
#   CMAKE_STRIP
#   CMAKE_INSTALL_NAME_TOOL

# on UNIX, cygwin and mingw

# Resolve full path of CMAKE_TOOL from user-defined name and SEARCH_PATH.
function(__resolve_tool_path CMAKE_TOOL SEARCH_PATH DOCSTRING)

  if(${CMAKE_TOOL})
    # We only get here if CMAKE_TOOL was
    # specified using -D or a pre-made CMakeCache.txt (e.g. via ctest)
    # or set in CMAKE_TOOLCHAIN_FILE.

    get_filename_component(_CMAKE_USER_TOOL_PATH "${${CMAKE_TOOL}}" DIRECTORY)
    # Is CMAKE_TOOL a user-defined name instead of a full path?
    if(NOT _CMAKE_USER_TOOL_PATH)

      # Find CMAKE_TOOL in the SEARCH_PATH directory by user-defined name.
      find_program(_CMAKE_TOOL_WITH_PATH NAMES ${${CMAKE_TOOL}} HINTS ${SEARCH_PATH})
      if(_CMAKE_TOOL_WITH_PATH)

        # Overwrite CMAKE_TOOL with full path found in SEARCH_PATH.
        set(${CMAKE_TOOL} ${_CMAKE_TOOL_WITH_PATH} PARENT_SCOPE)

        get_property(_CMAKE_TOOL_CACHED CACHE ${CMAKE_TOOL} PROPERTY TYPE)
        # If CMAKE_TOOL is present in the CMake Cache, then overwrit it as well.
        if(_CMAKE_TOOL_CACHED)
          set(${CMAKE_TOOL} "${_CMAKE_TOOL_WITH_PATH}" CACHE STRING ${DOCSTRING} FORCE)
        endif()

      endif()
      unset(_CMAKE_TOOL_WITH_PATH CACHE)

    endif()

  endif()

endfunction()

__resolve_tool_path(CMAKE_LINKER "${_CMAKE_TOOLCHAIN_LOCATION}" "Default Linker")
__resolve_tool_path(CMAKE_MT     "${_CMAKE_TOOLCHAIN_LOCATION}" "Default Manifest Tool")

set(_CMAKE_TOOL_VARS "")

# if it's the MS C/CXX compiler, search for link
if(("x${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_SIMULATE_ID}" STREQUAL "xMSVC" AND
   ("x${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_FRONTEND_VARIANT}" STREQUAL "xMSVC"
    OR NOT "x${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ID}" STREQUAL "xClang"))
   OR "x${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ID}" STREQUAL "xMSVC"
   OR (CMAKE_HOST_WIN32 AND "x${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ID}" STREQUAL "xPGI")
   OR (CMAKE_HOST_WIN32 AND "x${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ID}" STREQUAL "xNVIDIA")
   OR (CMAKE_HOST_WIN32 AND "x${_CMAKE_PROCESSING_LANGUAGE}" STREQUAL "xISPC")
   OR (CMAKE_GENERATOR MATCHES "Visual Studio"
       AND NOT CMAKE_VS_PLATFORM_NAME STREQUAL "Tegra-Android"))

  if("x${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ID}" STREQUAL "xClang")
    find_program(CMAKE_NM NAMES ${_CMAKE_TOOLCHAIN_PREFIX}nm llvm-nm HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
    set(_CMAKE_ADDITIONAL_LINKER_NAMES "lld-link")
    set(_CMAKE_ADDITIONAL_AR_NAMES "llvm-lib")
  endif()

  find_program(CMAKE_LINKER NAMES ${_CMAKE_ADDITIONAL_LINKER_NAMES} link HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
  find_program(CMAKE_AR     NAMES ${_CMAKE_ADDITIONAL_AR_NAMES}     lib  HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
  find_program(CMAKE_MT     NAMES mt   HINTS ${_CMAKE_TOOLCHAIN_LOCATION})

  list(APPEND _CMAKE_TOOL_VARS LINKER MT)

# in all other cases search for ar, ranlib, etc.
else()
  if(CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN)
    set(_CMAKE_TOOLCHAIN_LOCATION ${_CMAKE_TOOLCHAIN_LOCATION} ${CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN}/bin)
  endif()
  if(CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN)
    set(_CMAKE_TOOLCHAIN_LOCATION ${_CMAKE_TOOLCHAIN_LOCATION} ${CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN}/bin)
  endif()

  if("${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_COMPILER_ID}" STREQUAL Clang)
    set(_CMAKE_ADDITIONAL_AR_NAMES "llvm-ar")
    set(_CMAKE_ADDITIONAL_RANLIB_NAMES "llvm-ranlib")
    set(_CMAKE_ADDITIONAL_STRIP_NAMES "llvm-strip")
    if("x${CMAKE_${_CMAKE_PROCESSING_LANGUAGE}_SIMULATE_ID}" STREQUAL "xMSVC")
      set(_CMAKE_ADDITIONAL_LINKER_NAMES "lld-link")
    else()
      set(_CMAKE_ADDITIONAL_LINKER_NAMES "ld.lld")
    endif()
    set(_CMAKE_ADDITIONAL_NM_NAMES "llvm-nm")
    set(_CMAKE_ADDITIONAL_OBJDUMP_NAMES "llvm-objdump")
    set(_CMAKE_ADDITIONAL_OBJCOPY_NAMES "llvm-objcopy")
    set(_CMAKE_ADDITIONAL_READELF_NAMES "llvm-readelf")
    set(_CMAKE_ADDITIONAL_DLLTOOL_NAMES "llvm-dlltool")
    set(_CMAKE_ADDITIONAL_ADDR2LINE_NAMES "llvm-addr2line")
  endif()

  if(NOT CMAKE_CROSSCOMPILING AND NOT "${_CMAKE_TOOLCHAIN_PREFIX}" STREQUAL "")
    list(APPEND _CMAKE_ADDITIONAL_AR_NAMES "ar")
    list(APPEND _CMAKE_ADDITIONAL_RANLIB_NAMES "ranlib")
    list(APPEND _CMAKE_ADDITIONAL_STRIP_NAMES "strip")
    list(APPEND _CMAKE_ADDITIONAL_LINKER_NAMES "ld")
    list(APPEND _CMAKE_ADDITIONAL_NM_NAMES "nm")
    list(APPEND _CMAKE_ADDITIONAL_OBJDUMP_NAMES "objdump")
    list(APPEND _CMAKE_ADDITIONAL_OBJCOPY_NAMES "objcopy")
    list(APPEND _CMAKE_ADDITIONAL_READELF_NAMES "readelf")
    list(APPEND _CMAKE_ADDITIONAL_DLLTOOL_NAMES "dlltool")
    list(APPEND _CMAKE_ADDITIONAL_ADDR2LINE_NAMES "addr2line")
  endif()

  find_program(CMAKE_AR NAMES ${_CMAKE_TOOLCHAIN_PREFIX}ar${_CMAKE_TOOLCHAIN_SUFFIX} ${_CMAKE_ADDITIONAL_AR_NAMES} HINTS ${_CMAKE_TOOLCHAIN_LOCATION})

  find_program(CMAKE_RANLIB NAMES ${_CMAKE_TOOLCHAIN_PREFIX}ranlib ${_CMAKE_ADDITIONAL_RANLIB_NAMES} HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
  if(NOT CMAKE_RANLIB)
    set(CMAKE_RANLIB : CACHE INTERNAL "noop for ranlib")
  endif()


  find_program(CMAKE_STRIP NAMES ${_CMAKE_TOOLCHAIN_PREFIX}strip${_CMAKE_TOOLCHAIN_SUFFIX} ${_CMAKE_ADDITIONAL_STRIP_NAMES} HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
  find_program(CMAKE_LINKER NAMES ${_CMAKE_TOOLCHAIN_PREFIX}ld ${_CMAKE_ADDITIONAL_LINKER_NAMES} HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
  find_program(CMAKE_NM NAMES ${_CMAKE_TOOLCHAIN_PREFIX}nm ${_CMAKE_ADDITIONAL_NM_NAMES} HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
  find_program(CMAKE_OBJDUMP NAMES ${_CMAKE_TOOLCHAIN_PREFIX}objdump ${_CMAKE_ADDITIONAL_OBJDUMP_NAMES} HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
  find_program(CMAKE_OBJCOPY NAMES ${_CMAKE_TOOLCHAIN_PREFIX}objcopy ${_CMAKE_ADDITIONAL_OBJCOPY_NAMES} HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
  find_program(CMAKE_READELF NAMES ${_CMAKE_TOOLCHAIN_PREFIX}readelf ${_CMAKE_ADDITIONAL_READELF_NAMES} HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
  find_program(CMAKE_DLLTOOL NAMES ${_CMAKE_TOOLCHAIN_PREFIX}dlltool ${_CMAKE_ADDITIONAL_DLLTOOL_NAMES} HINTS ${_CMAKE_TOOLCHAIN_LOCATION})
  find_program(CMAKE_ADDR2LINE NAMES ${_CMAKE_TOOLCHAIN_PREFIX}addr2line ${_CMAKE_ADDITIONAL_ADDR2LINE_NAMES} HINTS ${_CMAKE_TOOLCHAIN_LOCATION})

  list(APPEND _CMAKE_TOOL_VARS AR RANLIB STRIP LINKER NM OBJDUMP OBJCOPY READELF DLLTOOL ADDR2LINE)
endif()

if(CMAKE_PLATFORM_HAS_INSTALLNAME)
  find_program(CMAKE_INSTALL_NAME_TOOL NAMES ${_CMAKE_TOOLCHAIN_PREFIX}install_name_tool HINTS ${_CMAKE_TOOLCHAIN_LOCATION})

  if(NOT CMAKE_INSTALL_NAME_TOOL)
    message(FATAL_ERROR "Could not find install_name_tool, please check your installation.")
  endif()

  list(APPEND _CMAKE_TOOL_VARS INSTALL_NAME_TOOL)
endif()

# Mark any tool cache entries as advanced.
foreach(var IN LISTS _CMAKE_TOOL_VARS)
  get_property(_CMAKE_TOOL_CACHED CACHE CMAKE_${var} PROPERTY TYPE)
  if(_CMAKE_TOOL_CACHED)
    mark_as_advanced(CMAKE_${var})
  endif()
  unset(_CMAKE_ADDITIONAL_${var}_NAMES)
endforeach()
unset(_CMAKE_TOOL_VARS)
unset(_CMAKE_TOOL_CACHED)
