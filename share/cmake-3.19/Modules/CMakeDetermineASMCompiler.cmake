# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# determine the compiler to use for ASM programs

include(${CMAKE_ROOT}/Modules/CMakeDetermineCompiler.cmake)

if(NOT CMAKE_ASM${ASM_DIALECT}_COMPILER)
  # prefer the environment variable ASM
  if(NOT $ENV{ASM${ASM_DIALECT}} STREQUAL "")
    get_filename_component(CMAKE_ASM${ASM_DIALECT}_COMPILER_INIT $ENV{ASM${ASM_DIALECT}} PROGRAM PROGRAM_ARGS CMAKE_ASM${ASM_DIALECT}_FLAGS_ENV_INIT)
    if(CMAKE_ASM${ASM_DIALECT}_FLAGS_ENV_INIT)
      set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ARG1 "${CMAKE_ASM${ASM_DIALECT}_FLAGS_ENV_INIT}" CACHE STRING "Arguments to ASM${ASM_DIALECT} compiler")
    endif()
    if(NOT EXISTS ${CMAKE_ASM${ASM_DIALECT}_COMPILER_INIT})
      message(FATAL_ERROR "Could not find compiler set in environment variable ASM${ASM_DIALECT}:\n$ENV{ASM${ASM_DIALECT}}.")
    endif()
  endif()

  # finally list compilers to try
  if("ASM${ASM_DIALECT}" STREQUAL "ASM") # the generic assembler support
    if(NOT CMAKE_ASM_COMPILER_INIT)
      if(CMAKE_C_COMPILER)
        set(CMAKE_ASM${ASM_DIALECT}_COMPILER_LIST ${CMAKE_C_COMPILER})
      elseif(CMAKE_CXX_COMPILER)
        set(CMAKE_ASM${ASM_DIALECT}_COMPILER_LIST ${CMAKE_CXX_COMPILER})
      else()
        # List all default C and CXX compilers
        set(CMAKE_ASM${ASM_DIALECT}_COMPILER_LIST
             ${_CMAKE_TOOLCHAIN_PREFIX}cc  ${_CMAKE_TOOLCHAIN_PREFIX}gcc cl bcc xlc
          CC ${_CMAKE_TOOLCHAIN_PREFIX}c++ ${_CMAKE_TOOLCHAIN_PREFIX}g++ aCC cl bcc xlC)
      endif()
    endif()
  else() # some specific assembler "dialect"
    if(NOT CMAKE_ASM${ASM_DIALECT}_COMPILER_INIT  AND NOT CMAKE_ASM${ASM_DIALECT}_COMPILER_LIST)
      message(FATAL_ERROR "CMAKE_ASM${ASM_DIALECT}_COMPILER_INIT or CMAKE_ASM${ASM_DIALECT}_COMPILER_LIST must be preset !")
    endif()
  endif()

  # Find the compiler.
  _cmake_find_compiler(ASM${ASM_DIALECT})

else()
  _cmake_find_compiler_path(ASM${ASM_DIALECT})
endif()
mark_as_advanced(CMAKE_ASM${ASM_DIALECT}_COMPILER)

if (NOT _CMAKE_TOOLCHAIN_LOCATION)
  get_filename_component(_CMAKE_TOOLCHAIN_LOCATION "${CMAKE_ASM${ASM_DIALECT}_COMPILER}" PATH)
endif ()


if(NOT CMAKE_ASM${ASM_DIALECT}_COMPILER_ID)

  # Table of per-vendor compiler id flags with expected output.
  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS GNU )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_GNU "--version")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_GNU "(GNU assembler)|(GCC)|(Free Software Foundation)")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS Clang )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_Clang "--version")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_Clang "(clang version)")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS AppleClang )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_AppleClang "--version")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_AppleClang "(Apple LLVM version)")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS ARMClang )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_ARMClang "--version")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_ARMClang "armclang")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS HP )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_HP "-V")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_HP "HP C")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS Intel )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_Intel "--version")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_Intel "(ICC)")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS SunPro )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_SunPro "-V")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_SunPro "Sun C")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS XL )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_XL "-qversion")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_XL "XL C")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS MSVC )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_MSVC "-?")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_MSVC "Microsoft")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS TI )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_TI "-h")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_TI "Texas Instruments")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS IAR)
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_IAR )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_IAR "IAR Assembler")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS ARMCC)
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_ARMCC )
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_ARMCC "(ARM Compiler)|(ARM Assembler)")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS NASM)
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_NASM "-v")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_NASM "(NASM version)")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS YASM)
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_YASM "--version")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_YASM "(yasm)")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS ADSP)
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_ADSP "-version")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_ADSP "Analog Devices")

  list(APPEND CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDORS QCC)
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_FLAGS_QCC "-V")
  set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_REGEX_QCC "gcc_nto")

  include(CMakeDetermineCompilerId)
  set(userflags)
  CMAKE_DETERMINE_COMPILER_ID_VENDOR(ASM${ASM_DIALECT} "${userflags}")
  if("x${CMAKE_ASM${ASM_DIALECT}_COMPILER_ID}" STREQUAL "xIAR")
    # primary necessary to detect architecture, so the right archiver and linker can be picked
    # eg. "IAR Assembler V8.10.1.12857/W32 for ARM" or "IAR Assembler V4.11.1.4666 for Renesas RX"
    # Cut out identification first, newline handling is a pain
    string(REGEX MATCH "IAR Assembler[^\r\n]*" _compileid "${CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_OUTPUT}")
    if("${_compileid}" MATCHES "V([0-9]+\\.[0-9]+\\.[0-9]+)")
      set(CMAKE_ASM${ASM_DIALECT}_COMPILER_VERSION ${CMAKE_MATCH_1})
    endif()
    string(REGEX MATCHALL "([A-Za-z0-9-]+)" _all_compileid_matches "${_compileid}")
    if(_all_compileid_matches)
      list(GET _all_compileid_matches "-1" CMAKE_ASM${ASM_DIALECT}_COMPILER_ARCHITECTURE_ID)
    endif()
  endif()

  _cmake_find_compiler_sysroot(ASM${ASM_DIALECT})

  unset(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_OUTPUT)
  unset(_all_compileid_matches)
  unset(_compileid)
endif()

if(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID)
  if(CMAKE_ASM${ASM_DIALECT}_COMPILER_VERSION)
    set(_version " ${CMAKE_ASM${ASM_DIALECT}_COMPILER_VERSION}")
  else()
    set(_version "")
  endif()
  if(CMAKE_ASM${ASM_DIALECT}_COMPILER_ARCHITECTURE_ID AND "x${CMAKE_ASM${ASM_DIALECT}_COMPILER_ID}" STREQUAL "xIAR")
    set(_archid " ${CMAKE_ASM${ASM_DIALECT}_COMPILER_ARCHITECTURE_ID}")
  else()
    set(_archid "")
  endif()
  message(STATUS "The ASM${ASM_DIALECT} compiler identification is ${CMAKE_ASM${ASM_DIALECT}_COMPILER_ID}${_archid}${_version}")
  unset(_archid)
  unset(_version)
else()
  message(STATUS "The ASM${ASM_DIALECT} compiler identification is unknown")
endif()

# If we have a gas/as cross compiler, they have usually some prefix, like
# e.g. powerpc-linux-gas, arm-elf-gas or i586-mingw32msvc-gas , optionally
# with a 3-component version number at the end
# The other tools of the toolchain usually have the same prefix
# NAME_WE cannot be used since then this test will fail for names like
# "arm-unknown-nto-qnx6.3.0-gas.exe", where BASENAME would be
# "arm-unknown-nto-qnx6" instead of the correct "arm-unknown-nto-qnx6.3.0-"
if (NOT _CMAKE_TOOLCHAIN_PREFIX)
  get_filename_component(COMPILER_BASENAME "${CMAKE_ASM${ASM_DIALECT}_COMPILER}" NAME)
  if (COMPILER_BASENAME MATCHES "^(.+-)g?as(-[0-9]+\\.[0-9]+\\.[0-9]+)?(\\.exe)?$")
    set(_CMAKE_TOOLCHAIN_PREFIX ${CMAKE_MATCH_1})
  endif ()
endif ()

# Now try the C compiler regexp:
if (NOT _CMAKE_TOOLCHAIN_PREFIX)
  if (COMPILER_BASENAME MATCHES "^(.+-)g?cc(-[0-9]+\\.[0-9]+\\.[0-9]+)?(\\.exe)?$")
    set(_CMAKE_TOOLCHAIN_PREFIX ${CMAKE_MATCH_1})
  endif ()
endif ()

# Finally try the CXX compiler regexp:
if (NOT _CMAKE_TOOLCHAIN_PREFIX)
  if (COMPILER_BASENAME MATCHES "^(.+-)[gc]\\+\\+(-[0-9]+\\.[0-9]+\\.[0-9]+)?(\\.exe)?$")
    set(_CMAKE_TOOLCHAIN_PREFIX ${CMAKE_MATCH_1})
  endif ()
endif ()


set(_CMAKE_PROCESSING_LANGUAGE "ASM")
include(CMakeFindBinUtils)
include(Compiler/${CMAKE_ASM${ASM_DIALECT}_COMPILER_ID}-FindBinUtils OPTIONAL)
unset(_CMAKE_PROCESSING_LANGUAGE)

set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ENV_VAR "ASM${ASM_DIALECT}")

if(CMAKE_ASM${ASM_DIALECT}_COMPILER)
  message(STATUS "Found assembler: ${CMAKE_ASM${ASM_DIALECT}_COMPILER}")
else()
  message(STATUS "Didn't find assembler")
endif()

foreach(_var
    COMPILER
    COMPILER_ID
    COMPILER_ARG1
    COMPILER_ENV_VAR
    COMPILER_AR
    COMPILER_RANLIB
    COMPILER_VERSION
    )
  set(_CMAKE_ASM_${_var} "${CMAKE_ASM${ASM_DIALECT}_${_var}}")
endforeach()

if(CMAKE_ASM${ASM_DIALECT}_COMPILER_SYSROOT)
  string(CONCAT _SET_CMAKE_ASM_COMPILER_SYSROOT
    "set(CMAKE_ASM${ASM_DIALECT}_COMPILER_SYSROOT \"${CMAKE_ASM${ASM_DIALECT}_COMPILER_SYSROOT}\")\n"
    "set(CMAKE_COMPILER_SYSROOT \"${CMAKE_ASM${ASM_DIALECT}_COMPILER_SYSROOT}\")")
else()
  set(_SET_CMAKE_ASM_COMPILER_SYSROOT "")
endif()

if(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_MATCH)
  set(_SET_CMAKE_ASM_COMPILER_ID_VENDOR_MATCH
    "set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_MATCH [==[${CMAKE_ASM${ASM_DIALECT}_COMPILER_ID_VENDOR_MATCH}]==])")
else()
  set(_SET_CMAKE_ASM_COMPILER_ID_VENDOR_MATCH "")
endif()

if(CMAKE_ASM${ASM_DIALECT}_COMPILER_ARCHITECTURE_ID)
  set(_SET_CMAKE_ASM_COMPILER_ARCHITECTURE_ID
    "set(CMAKE_ASM${ASM_DIALECT}_COMPILER_ARCHITECTURE_ID ${CMAKE_ASM${ASM_DIALECT}_COMPILER_ARCHITECTURE_ID})")
else()
  set(_SET_CMAKE_ASM_COMPILER_ARCHITECTURE_ID "")
endif()

# configure variables set in this file for fast reload later on
configure_file(${CMAKE_ROOT}/Modules/CMakeASMCompiler.cmake.in
  ${CMAKE_PLATFORM_INFO_DIR}/CMakeASM${ASM_DIALECT}Compiler.cmake @ONLY)

foreach(_var
    COMPILER
    COMPILER_ID
    COMPILER_ARG1
    COMPILER_ENV_VAR
    COMPILER_AR
    COMPILER_RANLIB
    COMPILER_VERSION
    )
  unset(_CMAKE_ASM_${_var})
endforeach()
