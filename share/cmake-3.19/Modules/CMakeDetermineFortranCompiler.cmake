# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# determine the compiler to use for Fortran programs
# NOTE, a generator may set CMAKE_Fortran_COMPILER before
# loading this file to force a compiler.
# use environment variable FC first if defined by user, next use
# the cmake variable CMAKE_GENERATOR_FC which can be defined by a generator
# as a default compiler

include(${CMAKE_ROOT}/Modules/CMakeDetermineCompiler.cmake)
include(Platform/${CMAKE_SYSTEM_NAME}-Determine-Fortran OPTIONAL)
include(Platform/${CMAKE_SYSTEM_NAME}-Fortran OPTIONAL)
if(NOT CMAKE_Fortran_COMPILER_NAMES)
  set(CMAKE_Fortran_COMPILER_NAMES f95)
endif()

if(${CMAKE_GENERATOR} MATCHES "Visual Studio")
elseif("${CMAKE_GENERATOR}" MATCHES "Xcode")
  set(CMAKE_Fortran_COMPILER_XCODE_TYPE sourcecode.fortran.f90)
  _cmake_find_compiler_path(Fortran)
else()
  if(NOT CMAKE_Fortran_COMPILER)
    # prefer the environment variable CC
    if(NOT $ENV{FC} STREQUAL "")
      get_filename_component(CMAKE_Fortran_COMPILER_INIT $ENV{FC} PROGRAM PROGRAM_ARGS CMAKE_Fortran_FLAGS_ENV_INIT)
      if(CMAKE_Fortran_FLAGS_ENV_INIT)
        set(CMAKE_Fortran_COMPILER_ARG1 "${CMAKE_Fortran_FLAGS_ENV_INIT}" CACHE STRING "Arguments to Fortran compiler")
      endif()
      if(EXISTS ${CMAKE_Fortran_COMPILER_INIT})
      else()
        message(FATAL_ERROR "Could not find compiler set in environment variable FC:\n$ENV{FC}.")
      endif()
    endif()

    # next try prefer the compiler specified by the generator
    if(CMAKE_GENERATOR_FC)
      if(NOT CMAKE_Fortran_COMPILER_INIT)
        set(CMAKE_Fortran_COMPILER_INIT ${CMAKE_GENERATOR_FC})
      endif()
    endif()

    # finally list compilers to try
    if(NOT CMAKE_Fortran_COMPILER_INIT)
      # Known compilers:
      #  f77/f90/f95: generic compiler names
      #  ftn: Cray fortran compiler wrapper
      #  g77: GNU Fortran 77 compiler
      #  gfortran: putative GNU Fortran 95+ compiler (in progress)
      #  fort77: native F77 compiler under HP-UX (and some older Crays)
      #  frt: Fujitsu F77 compiler
      #  pathf90/pathf95/pathf2003: PathScale Fortran compiler
      #  pgf77/pgf90/pgf95/pgfortran: Portland Group F77/F90/F95 compilers
      #  flang: Flang Fortran compiler
      #  xlf/xlf90/xlf95: IBM (AIX) F77/F90/F95 compilers
      #  lf95: Lahey-Fujitsu F95 compiler
      #  fl32: Microsoft Fortran 77 "PowerStation" compiler
      #  af77: Apogee F77 compiler for Intergraph hardware running CLIX
      #  epcf90: "Edinburgh Portable Compiler" F90
      #  fort: Compaq (now HP) Fortran 90/95 compiler for Tru64 and Linux/Alpha
      #  ifc: Intel Fortran 95 compiler for Linux/x86
      #  efc: Intel Fortran 95 compiler for IA64
      #  nagfor: NAG Fortran compiler
      #
      #  The order is 95 or newer compilers first, then 90,
      #  then 77 or older compilers, gnu is always last in the group,
      #  so if you paid for a compiler it is picked by default.
      if(CMAKE_HOST_WIN32)
        set(CMAKE_Fortran_COMPILER_LIST
          ifort pgf95 pgfortran lf95 fort
          flang gfortran gfortran-4 g95 f90 pgf90
          pgf77 g77 f77 nag
          )
      else()
        set(CMAKE_Fortran_COMPILER_LIST
          ftn
          ifort ifc efc pgf95 pgfortran lf95 xlf95 fort
          flang gfortran gfortran-4 g95 f90 pgf90
          frt pgf77 xlf g77 f77 nag
          )
      endif()

      # Vendor-specific compiler names.
      set(_Fortran_COMPILER_NAMES_GNU       gfortran gfortran-4 g95 g77)
      set(_Fortran_COMPILER_NAMES_Intel     ifort ifc efc)
      set(_Fortran_COMPILER_NAMES_Absoft    af95 af90 af77)
      set(_Fortran_COMPILER_NAMES_PGI       pgf95 pgfortran pgf90 pgf77)
      set(_Fortran_COMPILER_NAMES_Flang     flang)
      set(_Fortran_COMPILER_NAMES_PathScale pathf2003 pathf95 pathf90)
      set(_Fortran_COMPILER_NAMES_XL        xlf)
      set(_Fortran_COMPILER_NAMES_VisualAge xlf95 xlf90 xlf)
      set(_Fortran_COMPILER_NAMES_NAG       nagfor)
    endif()

    _cmake_find_compiler(Fortran)

  else()
    _cmake_find_compiler_path(Fortran)
  endif()
  mark_as_advanced(CMAKE_Fortran_COMPILER)

  # Each entry in this list is a set of extra flags to try
  # adding to the compile line to see if it helps produce
  # a valid identification executable.
  set(CMAKE_Fortran_COMPILER_ID_TEST_FLAGS_FIRST
    # Get verbose output to help distinguish compilers.
    "-v"
    )
  set(CMAKE_Fortran_COMPILER_ID_TEST_FLAGS
    # Try compiling to an object file only.
    "-c"

    # Intel on windows does not preprocess by default.
    "-fpp"
    )
endif()

# Build a small source file to identify the compiler.
if(NOT CMAKE_Fortran_COMPILER_ID_RUN)
  set(CMAKE_Fortran_COMPILER_ID_RUN 1)

  # Table of per-vendor compiler output regular expressions.
  list(APPEND CMAKE_Fortran_COMPILER_ID_MATCH_VENDORS CCur)
  set(CMAKE_Fortran_COMPILER_ID_MATCH_VENDOR_REGEX_CCur "Concurrent Fortran [0-9]+ Compiler")

  # Table of per-vendor compiler id flags with expected output.
  list(APPEND CMAKE_Fortran_COMPILER_ID_VENDORS Compaq)
  set(CMAKE_Fortran_COMPILER_ID_VENDOR_FLAGS_Compaq "-what")
  set(CMAKE_Fortran_COMPILER_ID_VENDOR_REGEX_Compaq "Compaq Visual Fortran")
  list(APPEND CMAKE_Fortran_COMPILER_ID_VENDORS NAG) # Numerical Algorithms Group
  set(CMAKE_Fortran_COMPILER_ID_VENDOR_FLAGS_NAG "-V")
  set(CMAKE_Fortran_COMPILER_ID_VENDOR_REGEX_NAG "NAG Fortran Compiler")

  # Match the link line from xcodebuild output of the form
  #  Ld ...
  #      ...
  #      /path/to/cc ...CompilerIdFortran/...
  # to extract the compiler front-end for the language.
  set(CMAKE_Fortran_COMPILER_ID_TOOL_MATCH_REGEX "\nLd[^\n]*(\n[ \t]+[^\n]*)*\n[ \t]+([^ \t\r\n]+)[^\r\n]*-o[^\r\n]*CompilerIdFortran/(\\./)?(CompilerIdFortran.xctest/)?CompilerIdFortran[ \t\n\\\"]")
  set(CMAKE_Fortran_COMPILER_ID_TOOL_MATCH_INDEX 2)

  set(_version_info "")
  foreach(m MAJOR MINOR PATCH TWEAK)
    set(_COMP "_${m}")
    string(APPEND _version_info "
#if defined(COMPILER_VERSION${_COMP})")
    foreach(d 1 2 3 4 5 6 7 8)
      string(APPEND _version_info "
# undef DEC
# undef HEX
# define DEC(n) DEC_${d}(n)
# define HEX(n) HEX_${d}(n)
# if COMPILER_VERSION${_COMP} == 0
        PRINT *, 'INFO:compiler_version${_COMP}_digit_${d}[0]'
# elif COMPILER_VERSION${_COMP} == 1
        PRINT *, 'INFO:compiler_version${_COMP}_digit_${d}[1]'
# elif COMPILER_VERSION${_COMP} == 2
        PRINT *, 'INFO:compiler_version${_COMP}_digit_${d}[2]'
# elif COMPILER_VERSION${_COMP} == 3
        PRINT *, 'INFO:compiler_version${_COMP}_digit_${d}[3]'
# elif COMPILER_VERSION${_COMP} == 4
        PRINT *, 'INFO:compiler_version${_COMP}_digit_${d}[4]'
# elif COMPILER_VERSION${_COMP} == 5
        PRINT *, 'INFO:compiler_version${_COMP}_digit_${d}[5]'
# elif COMPILER_VERSION${_COMP} == 6
        PRINT *, 'INFO:compiler_version${_COMP}_digit_${d}[6]'
# elif COMPILER_VERSION${_COMP} == 7
        PRINT *, 'INFO:compiler_version${_COMP}_digit_${d}[7]'
# elif COMPILER_VERSION${_COMP} == 8
        PRINT *, 'INFO:compiler_version${_COMP}_digit_${d}[8]'
# elif COMPILER_VERSION${_COMP} == 9
        PRINT *, 'INFO:compiler_version${_COMP}_digit_${d}[9]'
# endif
")
    endforeach()
    string(APPEND _version_info "
#endif")
  endforeach()
  set(CMAKE_Fortran_COMPILER_ID_VERSION_INFO "${_version_info}")
  unset(_version_info)
  unset(_COMP)

  # Try to identify the compiler.
  set(CMAKE_Fortran_COMPILER_ID)
  include(${CMAKE_ROOT}/Modules/CMakeDetermineCompilerId.cmake)
  CMAKE_DETERMINE_COMPILER_ID(Fortran FFLAGS CMakeFortranCompilerId.F)

  _cmake_find_compiler_sysroot(Fortran)

  # Fall back to old is-GNU test.
  if(NOT CMAKE_Fortran_COMPILER_ID)
    execute_process(COMMAND ${CMAKE_Fortran_COMPILER} ${CMAKE_Fortran_COMPILER_ID_FLAGS_LIST} -E "${CMAKE_ROOT}/Modules/CMakeTestGNU.c"
      OUTPUT_VARIABLE CMAKE_COMPILER_OUTPUT RESULT_VARIABLE CMAKE_COMPILER_RETURN)
    if(NOT CMAKE_COMPILER_RETURN)
      if(CMAKE_COMPILER_OUTPUT MATCHES "THIS_IS_GNU")
        set(CMAKE_Fortran_COMPILER_ID "GNU")
        file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
          "Determining if the Fortran compiler is GNU succeeded with "
          "the following output:\n${CMAKE_COMPILER_OUTPUT}\n\n")
      else()
        file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
          "Determining if the Fortran compiler is GNU failed with "
          "the following output:\n${CMAKE_COMPILER_OUTPUT}\n\n")
      endif()
      if(NOT CMAKE_Fortran_PLATFORM_ID)
        if(CMAKE_COMPILER_OUTPUT MATCHES "THIS_IS_MINGW")
          set(CMAKE_Fortran_PLATFORM_ID "MinGW")
        endif()
        if(CMAKE_COMPILER_OUTPUT MATCHES "THIS_IS_CYGWIN")
          set(CMAKE_Fortran_PLATFORM_ID "Cygwin")
        endif()
      endif()
    endif()
  endif()

  # Fall back for GNU MINGW, which is not always detected correctly
  # (__MINGW32__ is defined for the C language, but perhaps not for Fortran!)
  if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU" AND NOT CMAKE_Fortran_PLATFORM_ID)
    execute_process(COMMAND ${CMAKE_Fortran_COMPILER} ${CMAKE_Fortran_COMPILER_ID_FLAGS_LIST} -E "${CMAKE_ROOT}/Modules/CMakeTestGNU.c"
      OUTPUT_VARIABLE CMAKE_COMPILER_OUTPUT RESULT_VARIABLE CMAKE_COMPILER_RETURN)
    if(NOT CMAKE_COMPILER_RETURN)
      if(CMAKE_COMPILER_OUTPUT MATCHES "THIS_IS_MINGW")
        set(CMAKE_Fortran_PLATFORM_ID "MinGW")
      endif()
      if(CMAKE_COMPILER_OUTPUT MATCHES "THIS_IS_CYGWIN")
        set(CMAKE_Fortran_PLATFORM_ID "Cygwin")
      endif()
    endif()
  endif()

  # Set old compiler and platform id variables.
  if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU")
    set(CMAKE_COMPILER_IS_GNUG77 1)
  endif()
  if(CMAKE_Fortran_PLATFORM_ID MATCHES "MinGW")
    set(CMAKE_COMPILER_IS_MINGW 1)
  elseif(CMAKE_Fortran_PLATFORM_ID MATCHES "Cygwin")
    set(CMAKE_COMPILER_IS_CYGWIN 1)
  endif()
endif()

if (NOT _CMAKE_TOOLCHAIN_LOCATION)
  get_filename_component(_CMAKE_TOOLCHAIN_LOCATION "${CMAKE_Fortran_COMPILER}" PATH)
endif ()

# if we have a fortran cross compiler, they have usually some prefix, like
# e.g. powerpc-linux-gfortran, arm-elf-gfortran or i586-mingw32msvc-gfortran , optionally
# with a 3-component version number at the end (e.g. arm-eabi-gcc-4.5.2).
# The other tools of the toolchain usually have the same prefix
# NAME_WE cannot be used since then this test will fail for names like
# "arm-unknown-nto-qnx6.3.0-gcc.exe", where BASENAME would be
# "arm-unknown-nto-qnx6" instead of the correct "arm-unknown-nto-qnx6.3.0-"
if (NOT _CMAKE_TOOLCHAIN_PREFIX)

  if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU")
    get_filename_component(COMPILER_BASENAME "${CMAKE_Fortran_COMPILER}" NAME)
    if (COMPILER_BASENAME MATCHES "^(.+-)g?fortran(-[0-9]+\\.[0-9]+\\.[0-9]+)?(\\.exe)?$")
      set(_CMAKE_TOOLCHAIN_PREFIX ${CMAKE_MATCH_1})
    endif ()

    # if "llvm-" is part of the prefix, remove it, since llvm doesn't have its own binutils
    # but uses the regular ar, objcopy, etc. (instead of llvm-objcopy etc.)
    if ("${_CMAKE_TOOLCHAIN_PREFIX}" MATCHES "(.+-)?llvm-$")
      set(_CMAKE_TOOLCHAIN_PREFIX ${CMAKE_MATCH_1})
    endif ()
  endif()

endif ()

set(_CMAKE_PROCESSING_LANGUAGE "Fortran")
include(CMakeFindBinUtils)
include(Compiler/${CMAKE_Fortran_COMPILER_ID}-FindBinUtils OPTIONAL)
unset(_CMAKE_PROCESSING_LANGUAGE)

if(CMAKE_Fortran_XL_CPP)
  set(_SET_CMAKE_Fortran_XL_CPP
    "set(CMAKE_Fortran_XL_CPP \"${CMAKE_Fortran_XL_CPP}\")")
endif()

if(CMAKE_Fortran_COMPILER_SYSROOT)
  string(CONCAT _SET_CMAKE_Fortran_COMPILER_SYSROOT
    "set(CMAKE_Fortran_COMPILER_SYSROOT \"${CMAKE_Fortran_COMPILER_SYSROOT}\")\n"
    "set(CMAKE_COMPILER_SYSROOT \"${CMAKE_Fortran_COMPILER_SYSROOT}\")")
else()
  set(_SET_CMAKE_Fortran_COMPILER_SYSROOT "")
endif()

if(CMAKE_Fortran_COMPILER_ARCHITECTURE_ID)
  set(_SET_CMAKE_Fortran_COMPILER_ARCHITECTURE_ID
    "set(CMAKE_Fortran_COMPILER_ARCHITECTURE_ID ${CMAKE_Fortran_COMPILER_ARCHITECTURE_ID})")
else()
  set(_SET_CMAKE_Fortran_COMPILER_ARCHITECTURE_ID "")
endif()

if(MSVC_Fortran_ARCHITECTURE_ID)
  set(SET_MSVC_Fortran_ARCHITECTURE_ID
    "set(MSVC_Fortran_ARCHITECTURE_ID ${MSVC_Fortran_ARCHITECTURE_ID})")
endif()
# configure variables set in this file for fast reload later on
configure_file(${CMAKE_ROOT}/Modules/CMakeFortranCompiler.cmake.in
  ${CMAKE_PLATFORM_INFO_DIR}/CMakeFortranCompiler.cmake
  @ONLY
  )
set(CMAKE_Fortran_COMPILER_ENV_VAR "FC")
