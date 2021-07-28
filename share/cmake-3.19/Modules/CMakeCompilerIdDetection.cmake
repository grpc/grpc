# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


function(_readFile file)
  include(${file})
  get_filename_component(name ${file} NAME_WE)
  string(REGEX REPLACE "-.*" "" CompilerId ${name})
  set(_compiler_id_version_compute_${CompilerId} ${_compiler_id_version_compute} PARENT_SCOPE)
  set(_compiler_id_simulate_${CompilerId} ${_compiler_id_simulate} PARENT_SCOPE)
  set(_compiler_id_pp_test_${CompilerId} ${_compiler_id_pp_test} PARENT_SCOPE)
endfunction()

function(compiler_id_detection outvar lang)

  if (NOT lang STREQUAL Fortran AND NOT lang STREQUAL CSharp
      AND NOT lang STREQUAL ISPC)
    file(GLOB lang_files
      "${CMAKE_ROOT}/Modules/Compiler/*-DetermineCompiler.cmake")
    set(nonlang CXX)
    if (lang STREQUAL CXX)
      set(nonlang C)
    endif()

    file(GLOB nonlang_files
      "${CMAKE_ROOT}/Modules/Compiler/*-${nonlang}-DetermineCompiler.cmake")
    list(REMOVE_ITEM lang_files ${nonlang_files})
  endif()

  set(files ${lang_files})
  if (files)
    foreach(file ${files})
      _readFile(${file})
    endforeach()

    set(options ID_STRING VERSION_STRINGS ID_DEFINE PLATFORM_DEFAULT_COMPILER)
    set(oneValueArgs PREFIX)
    cmake_parse_arguments(CID "${options}" "${oneValueArgs}" "${multiValueArgs}"  ${ARGN})
    if (CID_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "Unrecognized arguments: \"${CID_UNPARSED_ARGUMENTS}\"")
    endif()

    # Order is relevant here. For example, compilers which pretend to be
    # GCC must appear before the actual GCC.
    if (lang STREQUAL CXX)
      list(APPEND ordered_compilers
        Comeau
      )
    endif()
    list(APPEND ordered_compilers
      Intel
      PathScale
      Embarcadero
      Borland
      Watcom
      OpenWatcom
      SunPro
      HP
      Compaq
      zOS
      XLClang
      XL
      VisualAge
      PGI
      Cray
      TI
      Fujitsu
      GHS
    )
    if (lang STREQUAL C)
      list(APPEND ordered_compilers
        TinyCC
        Bruce
      )
    endif()
    list(APPEND ordered_compilers
      SCO
      ARMCC
      AppleClang
      ARMClang
      Clang
      GNU
      MSVC
      ADSP
      IAR
    )
    if (lang STREQUAL C)
      list(APPEND ordered_compilers
        SDCC
      )
    endif()

    if(lang STREQUAL CUDA)
      set(ordered_compilers NVIDIA Clang)
    endif()

    if(CID_ID_DEFINE)
      foreach(Id ${ordered_compilers})
        string(APPEND CMAKE_${lang}_COMPILER_ID_CONTENT "# define ${CID_PREFIX}COMPILER_IS_${Id} 0\n")
      endforeach()
      # Hard-code definitions for compilers that are no longer supported.
      string(APPEND CMAKE_${lang}_COMPILER_ID_CONTENT "# define ${CID_PREFIX}COMPILER_IS_MIPSpro 0\n")
    endif()

    set(pp_if "#if")
    if (CID_VERSION_STRINGS)
      string(APPEND CMAKE_${lang}_COMPILER_ID_CONTENT "\n/* Version number components: V=Version, R=Revision, P=Patch
   Version date components:   YYYY=Year, MM=Month,   DD=Day  */\n")
    endif()

    foreach(Id ${ordered_compilers})
      if (NOT _compiler_id_pp_test_${Id})
        message(FATAL_ERROR "No preprocessor test for \"${Id}\"")
      endif()
      set(id_content "${pp_if} ${_compiler_id_pp_test_${Id}}\n")
      if (CID_ID_STRING)
        set(PREFIX ${CID_PREFIX})
        string(CONFIGURE "${_compiler_id_simulate_${Id}}" SIMULATE_BLOCK @ONLY)
        string(APPEND id_content "# define ${CID_PREFIX}COMPILER_ID \"${Id}\"${SIMULATE_BLOCK}")
      endif()
      if (CID_ID_DEFINE)
        string(APPEND id_content "# undef ${CID_PREFIX}COMPILER_IS_${Id}\n")
        string(APPEND id_content "# define ${CID_PREFIX}COMPILER_IS_${Id} 1\n")
      endif()
      if (CID_VERSION_STRINGS)
        set(PREFIX ${CID_PREFIX})
        set(MACRO_DEC DEC)
        set(MACRO_HEX HEX)
        string(CONFIGURE "${_compiler_id_version_compute_${Id}}" VERSION_BLOCK @ONLY)
        string(APPEND id_content "${VERSION_BLOCK}\n")
      endif()
      string(APPEND CMAKE_${lang}_COMPILER_ID_CONTENT "\n${id_content}")
      set(pp_if "#elif")
    endforeach()

    if (CID_PLATFORM_DEFAULT_COMPILER)
      set(platform_compiler_detection "
/* These compilers are either not known or too old to define an
  identification macro.  Try to identify the platform and guess that
  it is the native compiler.  */
#elif defined(__hpux) || defined(__hpua)
# define ${CID_PREFIX}COMPILER_ID \"HP\"

#else /* unknown compiler */
# define ${CID_PREFIX}COMPILER_ID \"\"")
    endif()

    string(APPEND CMAKE_${lang}_COMPILER_ID_CONTENT "\n${platform_compiler_detection}\n#endif")
  endif()

  set(${outvar} ${CMAKE_${lang}_COMPILER_ID_CONTENT} PARENT_SCOPE)
endfunction()
