# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# This module is shared by multiple languages; use include blocker.
if(__COMPILER_GNU)
  return()
endif()
set(__COMPILER_GNU 1)

include(Compiler/CMakeCommonCompilerMacros)

set(__pch_header_C "c-header")
set(__pch_header_CXX "c++-header")
set(__pch_header_OBJC "objective-c-header")
set(__pch_header_OBJCXX "objective-c++-header")

macro(__compiler_gnu lang)
  # Feature flags.
  set(CMAKE_${lang}_VERBOSE_FLAG "-v")
  set(CMAKE_${lang}_COMPILE_OPTIONS_PIC "-fPIC")
  set (_CMAKE_${lang}_PIE_MAY_BE_SUPPORTED_BY_LINKER NO)
  if(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 3.4)
    set(CMAKE_${lang}_COMPILE_OPTIONS_PIE "-fPIE")
    # Support of PIE at link stage depends on various elements : platform, compiler, linker
    # so to activate it, module CheckPIESupported must be used.
    set (_CMAKE_${lang}_PIE_MAY_BE_SUPPORTED_BY_LINKER YES)
    set(CMAKE_${lang}_LINK_OPTIONS_PIE ${CMAKE_${lang}_COMPILE_OPTIONS_PIE} "-pie")
    set(CMAKE_${lang}_LINK_OPTIONS_NO_PIE "-no-pie")
  endif()
  if(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 4.0)
    set(CMAKE_${lang}_COMPILE_OPTIONS_VISIBILITY "-fvisibility=")
  endif()
  set(CMAKE_SHARED_LIBRARY_${lang}_FLAGS "-fPIC")
  set(CMAKE_SHARED_LIBRARY_CREATE_${lang}_FLAGS "-shared")
  set(CMAKE_${lang}_COMPILE_OPTIONS_SYSROOT "--sysroot=")

  set(CMAKE_${lang}_LINKER_WRAPPER_FLAG "-Wl,")
  set(CMAKE_${lang}_LINKER_WRAPPER_FLAG_SEP ",")

  # Older versions of gcc (< 4.5) contain a bug causing them to report a missing
  # header file as a warning if depfiles are enabled, causing check_header_file
  # tests to always succeed.  Work around this by disabling dependency tracking
  # in try_compile mode.
  get_property(_IN_TC GLOBAL PROPERTY IN_TRY_COMPILE)
  if(CMAKE_${lang}_COMPILER_ID STREQUAL "GNU" AND _IN_TC AND NOT CMAKE_FORCE_DEPFILES)
  else()
    # distcc does not transform -o to -MT when invoking the preprocessor
    # internally, as it ought to.  Work around this bug by setting -MT here
    # even though it isn't strictly necessary.
    set(CMAKE_DEPFILE_FLAGS_${lang} "-MD -MT <OBJECT> -MF <DEPFILE>")
  endif()

  # Initial configuration flags.
  string(APPEND CMAKE_${lang}_FLAGS_INIT " ")
  string(APPEND CMAKE_${lang}_FLAGS_DEBUG_INIT " -g")
  string(APPEND CMAKE_${lang}_FLAGS_MINSIZEREL_INIT " -Os -DNDEBUG")
  string(APPEND CMAKE_${lang}_FLAGS_RELEASE_INIT " -O3 -DNDEBUG")
  string(APPEND CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT " -O2 -g -DNDEBUG")
  set(CMAKE_${lang}_CREATE_PREPROCESSED_SOURCE "<CMAKE_${lang}_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -E <SOURCE> > <PREPROCESSED_SOURCE>")
  set(CMAKE_${lang}_CREATE_ASSEMBLY_SOURCE "<CMAKE_${lang}_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -S <SOURCE> -o <ASSEMBLY_SOURCE>")
  if(NOT APPLE OR NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 4) # work around #4462
    set(CMAKE_INCLUDE_SYSTEM_FLAG_${lang} "-isystem ")
  endif()

  set(_CMAKE_${lang}_IPO_SUPPORTED_BY_CMAKE YES)
  set(_CMAKE_${lang}_IPO_MAY_BE_SUPPORTED_BY_COMPILER NO)

  # '-flto' introduced since GCC 4.5:
  # * https://gcc.gnu.org/onlinedocs/gcc-4.4.7/gcc/Option-Summary.html (no)
  # * https://gcc.gnu.org/onlinedocs/gcc-4.5.4/gcc/Option-Summary.html (yes)
  if(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 4.5)
    set(_CMAKE_${lang}_IPO_MAY_BE_SUPPORTED_BY_COMPILER YES)
    set(__lto_flags -flto)

    if(NOT CMAKE_${lang}_COMPILER_VERSION VERSION_LESS 4.7)
      # '-ffat-lto-objects' introduced since GCC 4.7:
      # * https://gcc.gnu.org/onlinedocs/gcc-4.6.4/gcc/Option-Summary.html (no)
      # * https://gcc.gnu.org/onlinedocs/gcc-4.7.4/gcc/Option-Summary.html (yes)
      list(APPEND __lto_flags -fno-fat-lto-objects)
    endif()

    set(CMAKE_${lang}_COMPILE_OPTIONS_IPO ${__lto_flags})

    # Need to use version of 'ar'/'ranlib' with plugin support.
    # Quote from [documentation][1]:
    #
    #   To create static libraries suitable for LTO,
    #   use gcc-ar and gcc-ranlib instead of ar and ranlib
    #
    # [1]: https://gcc.gnu.org/onlinedocs/gcc-4.9.4/gcc/Optimize-Options.html
    set(CMAKE_${lang}_ARCHIVE_CREATE_IPO
      "\"${CMAKE_${lang}_COMPILER_AR}\" cr <TARGET> <LINK_FLAGS> <OBJECTS>"
    )

    set(CMAKE_${lang}_ARCHIVE_APPEND_IPO
      "\"${CMAKE_${lang}_COMPILER_AR}\" r <TARGET> <LINK_FLAGS> <OBJECTS>"
    )

    set(CMAKE_${lang}_ARCHIVE_FINISH_IPO
      "\"${CMAKE_${lang}_COMPILER_RANLIB}\" <TARGET>"
    )
  endif()

  set(CMAKE_${lang}_COMPILER_PREDEFINES_COMMAND "${CMAKE_${lang}_COMPILER}")
  if(CMAKE_${lang}_COMPILER_ARG1)
    separate_arguments(_COMPILER_ARGS NATIVE_COMMAND "${CMAKE_${lang}_COMPILER_ARG1}")
    list(APPEND CMAKE_${lang}_COMPILER_PREDEFINES_COMMAND ${_COMPILER_ARGS})
    unset(_COMPILER_ARGS)
  endif()
  list(APPEND CMAKE_${lang}_COMPILER_PREDEFINES_COMMAND "-dM" "-E" "-c" "${CMAKE_ROOT}/Modules/CMakeCXXCompilerABI.cpp")

  if(NOT "x${lang}" STREQUAL "xFortran")
    set(CMAKE_PCH_EXTENSION .gch)
    if (NOT CMAKE_GENERATOR MATCHES "Xcode")
      set(CMAKE_PCH_PROLOGUE "#pragma GCC system_header")
    endif()
    set(CMAKE_${lang}_COMPILE_OPTIONS_INVALID_PCH -Winvalid-pch)
    set(CMAKE_${lang}_COMPILE_OPTIONS_USE_PCH -include <PCH_HEADER>)
    set(CMAKE_${lang}_COMPILE_OPTIONS_CREATE_PCH -x ${__pch_header_${lang}} -include <PCH_HEADER>)
  endif()
endmacro()
