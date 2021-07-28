# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindOpenACC
-----------

.. versionadded:: 3.10

Detect OpenACC support by the compiler.

This module can be used to detect OpenACC support in a compiler.
If the compiler supports OpenACC, the flags required to compile with
OpenACC support are returned in variables for the different languages.
Currently, only PGI, GNU and Cray compilers are supported.

Variables
^^^^^^^^^

This module will set the following variables per language in your
project, where ``<lang>`` is one of C, CXX, or Fortran:

``OpenACC_<lang>_FOUND``
  Variable indicating if OpenACC support for ``<lang>`` was detected.
``OpenACC_<lang>_FLAGS``
  OpenACC compiler flags for ``<lang>``, separated by spaces.
``OpenACC_<lang>_OPTIONS``
  OpenACC compiler flags for ``<lang>``, as a list. Suitable for usage
  with target_compile_options or target_link_options.

Additionally, the module provides :prop_tgt:`IMPORTED` targets:

``OpenACC::OpenACC_<lang>``
  Target for using OpenACC from ``<lang>``.

The module will also try to provide the OpenACC version variables:

``OpenACC_<lang>_SPEC_DATE``
  Date of the OpenACC specification implemented by the ``<lang>`` compiler.
``OpenACC_<lang>_VERSION_MAJOR``
  Major version of OpenACC implemented by the ``<lang>`` compiler.
``OpenACC_<lang>_VERSION_MINOR``
  Minor version of OpenACC implemented by the ``<lang>`` compiler.
``OpenACC_<lang>_VERSION``
  OpenACC version implemented by the ``<lang>`` compiler.

The specification date is formatted as given in the OpenACC standard:
``yyyymm`` where ``yyyy`` and ``mm`` represents the year and month of
the OpenACC specification implemented by the ``<lang>`` compiler.

Input Variables
^^^^^^^^^^^^^^^

``OpenACC_ACCEL_TARGET=<target>``
If set, will the correct target accelerator flag set to the <target> will
be returned with OpenACC_<lang>_FLAGS.
#]=======================================================================]

set(OpenACC_C_CXX_TEST_SOURCE
"
int main(){
#ifdef _OPENACC
  return 0;
#else
  breaks_on_purpose
#endif
}
"
)
set(OpenACC_Fortran_TEST_SOURCE
"
program test
#ifndef _OPENACC
  breaks_on_purpose
#endif
endprogram test
"
)
set(OpenACC_C_CXX_CHECK_VERSION_SOURCE
"
#include <stdio.h>
const char accver_str[] = { 'I', 'N', 'F', 'O', ':', 'O', 'p', 'e', 'n', 'A',
                            'C', 'C', '-', 'd', 'a', 't', 'e', '[',
                            ('0' + ((_OPENACC/100000)%10)),
                            ('0' + ((_OPENACC/10000)%10)),
                            ('0' + ((_OPENACC/1000)%10)),
                            ('0' + ((_OPENACC/100)%10)),
                            ('0' + ((_OPENACC/10)%10)),
                            ('0' + ((_OPENACC/1)%10)),
                            ']', '\\0' };
int main()
{
  puts(accver_str);
  return 0;
}
")
set(OpenACC_Fortran_CHECK_VERSION_SOURCE
"
      program acc_ver
      implicit none
      integer, parameter :: zero = ichar('0')
      character, dimension(25), parameter :: accver_str =&
      (/ 'I', 'N', 'F', 'O', ':', 'O', 'p', 'e', 'n', 'A', 'C', 'C', '-',&
         'd', 'a', 't', 'e', '[',&
         char(zero + mod(_OPENACC/100000, 10)),&
         char(zero + mod(_OPENACC/10000, 10)),&
         char(zero + mod(_OPENACC/1000, 10)),&
         char(zero + mod(_OPENACC/100, 10)),&
         char(zero + mod(_OPENACC/10, 10)),&
         char(zero + mod(_OPENACC/1, 10)), ']' /)
      print *, accver_str
      end program acc_ver
"
)


function(_OPENACC_WRITE_SOURCE_FILE LANG SRC_FILE_CONTENT_VAR SRC_FILE_NAME SRC_FILE_FULLPATH)
  set(WORK_DIR ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/FindOpenACC)
  if("${LANG}" STREQUAL "C")
    set(SRC_FILE "${WORK_DIR}/${SRC_FILE_NAME}.c")
    file(WRITE "${SRC_FILE}" "${OpenACC_C_CXX_${SRC_FILE_CONTENT_VAR}}")
  elseif("${LANG}" STREQUAL "CXX")
    set(SRC_FILE "${WORK_DIR}/${SRC_FILE_NAME}.cpp")
    file(WRITE "${SRC_FILE}" "${OpenACC_C_CXX_${SRC_FILE_CONTENT_VAR}}")
  elseif("${LANG}" STREQUAL "Fortran")
    set(SRC_FILE "${WORK_DIR}/${SRC_FILE_NAME}.F90")
    file(WRITE "${SRC_FILE}_in" "${OpenACC_Fortran_${SRC_FILE_CONTENT_VAR}}")
    configure_file("${SRC_FILE}_in" "${SRC_FILE}" @ONLY)
  endif()
  set(${SRC_FILE_FULLPATH} "${SRC_FILE}" PARENT_SCOPE)
endfunction()


function(_OPENACC_GET_FLAGS_CANDIDATE LANG FLAG_VAR)
  set(ACC_FLAG_PGI "-acc")
  set(ACC_FLAG_GNU "-fopenacc")
  set(ACC_FLAG_Cray "-h acc")

  if(DEFINED ACC_FLAG_${CMAKE_${LANG}_COMPILER_ID})
    set("${FLAG_VAR}" "${ACC_FLAG_${CMAKE_${LANG}_COMPILER_ID}}" PARENT_SCOPE)
  else()
    # Fall back to a few common flags.
    set("${FLAG_VAR}" ${ACC_FLAG_GNU} ${ACC_FLAG_PGI})
  endif()

endfunction()


function(_OPENACC_GET_ACCEL_TARGET_FLAG LANG TARGET FLAG_VAR)
  # Find target accelerator flags.
  set(ACC_TARGET_FLAG_PGI "-ta")
  if(DEFINED ACC_TARGET_FLAG_${CMAKE_${LANG}_COMPILER_ID})
    set("${FLAG_VAR}" "${ACC_TARGET_FLAG_${CMAKE_${LANG}_COMPILER_ID}}=${TARGET}" PARENT_SCOPE)
  endif()
endfunction()


function(_OPENACC_GET_VERBOSE_FLAG LANG FLAG_VAR)
  # Find compiler's verbose flag for OpenACC.
  set(ACC_VERBOSE_FLAG_PGI "-Minfo=accel")
  if(DEFINED ACC_VERBOSE_FLAG_${CMAKE_${LANG}_COMPILER_ID})
    set("${FLAG_VAR}" "${ACC_VERBOSE_FLAG_${CMAKE_${LANG}_COMPILER_ID}}" PARENT_SCOPE)
  endif()
endfunction()


function(_OPENACC_GET_FLAGS LANG FLAG_VAR)
  set(FLAG_CANDIDATES "")
  _OPENACC_GET_FLAGS_CANDIDATE("${LANG}" FLAG_CANDIDATES)
  _OPENACC_WRITE_SOURCE_FILE("${LANG}" "TEST_SOURCE" OpenACCTryFlag _OPENACC_TEST_SRC)

  foreach(FLAG IN LISTS FLAG_CANDIDATES)
    try_compile(OpenACC_FLAG_TEST_RESULT ${CMAKE_BINARY_DIR} ${_OPENACC_TEST_SRC}
      CMAKE_FLAGS "-DCOMPILE_DEFINITIONS:STRING=${FLAG}"
      OUTPUT_VARIABLE OpenACC_TRY_COMPILE_OUTPUT
    )
    if(OpenACC_FLAG_TEST_RESULT)
      set("${FLAG_VAR}" "${FLAG}")
      if(DEFINED OpenACC_ACCEL_TARGET)
        _OPENACC_GET_ACCEL_TARGET_FLAG("${LANG}" "${OpenACC_ACCEL_TARGET}" TARGET_FLAG)
        string(APPEND "${FLAG_VAR}" " ${TARGET_FLAG}")
      endif()

      if(CMAKE_VERBOSE_MAKEFILE)
        # -Minfo=accel prints out OpenACC's messages on optimizations.
        _OPENACC_GET_VERBOSE_FLAG("${LANG}" OpenACC_VERBOSE_FLAG)
        string(APPEND "${FLAG_VAR}" " ${OpenACC_VERBOSE_FLAG}")
      endif()
      set("${FLAG_VAR}" "${${FLAG_VAR}}" PARENT_SCOPE)
      break()
    endif()
  endforeach()

endfunction()


function(_OPENACC_GET_SPEC_DATE LANG SPEC_DATE)
  _OPENACC_WRITE_SOURCE_FILE("${LANG}" "CHECK_VERSION_SOURCE" OpenACCCheckVersion _OPENACC_TEST_SRC)

  set(BIN_FILE "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/FindOpenACC/accver_${LANG}.bin")
  try_compile(OpenACC_SPECTEST_${LANG} "${CMAKE_BINARY_DIR}" "${_OPENACC_TEST_SRC}"
              CMAKE_FLAGS "-DCOMPILE_DEFINITIONS:STRING=${OpenACC_${LANG}_FLAGS}"
              COPY_FILE ${BIN_FILE}
              OUTPUT_VARIABLE OUTPUT)

  if(${OpenACC_SPECTEST_${LANG}})
    file(STRINGS ${BIN_FILE} specstr LIMIT_COUNT 1 REGEX "INFO:OpenACC-date")
    set(regex_spec_date ".*INFO:OpenACC-date\\[0*([^]]*)\\].*")
    if("${specstr}" MATCHES "${regex_spec_date}")
      set(${SPEC_DATE} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    endif()
  endif()
endfunction()


macro(_OPENACC_SET_VERSION_BY_SPEC_DATE LANG)
  set(OpenACC_SPEC_DATE_MAP
    # Combined versions, 2.5 onwards
    "201510=2.5"
    # 2013 08 is the corrected version.
    "201308=2.0"
    "201306=2.0"
    "201111=1.0"
  )

  string(REGEX MATCHALL "${OpenACC_${LANG}_SPEC_DATE}=([0-9]+)\\.([0-9]+)" _version_match "${OpenACC_SPEC_DATE_MAP}")
  if(NOT _version_match STREQUAL "")
    set(OpenACC_${LANG}_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(OpenACC_${LANG}_VERSION_MINOR ${CMAKE_MATCH_2})
    set(OpenACC_${LANG}_VERSION "${OpenACC_${LANG}_VERSION_MAJOR}.${OpenACC_${LANG}_VERSION_MINOR}")
  else()
    unset(OpenACC_${LANG}_VERSION_MAJOR)
    unset(OpenACC_${LANG}_VERSION_MINOR)
    unset(OpenACC_${LANG}_VERSION)
  endif()
  unset(_version_match)
  unset(OpenACC_SPEC_DATE_MAP)
endmacro()


include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
foreach (LANG IN ITEMS C CXX Fortran)
  if(CMAKE_${LANG}_COMPILER_LOADED)
    set(OpenACC_${LANG}_FIND_QUIETLY ${OpenACC_FIND_QUIETLY})
    set(OpenACC_${LANG}_FIND_REQUIRED ${OpenACC_FIND_REQUIRED})
    set(OpenACC_${LANG}_FIND_VERSION ${OpenACC_FIND_VERSION})
    set(OpenACC_${LANG}_FIND_VERSION_EXACT ${OpenACC_FIND_VERSION_EXACT})

    if(NOT DEFINED OpenACC_${LANG}_FLAGS)
      _OPENACC_GET_FLAGS("${LANG}" OpenACC_${LANG}_FLAGS)
    endif()
    if(NOT DEFINED OpenACC_${LANG}_OPTIONS)
      separate_arguments(OpenACC_${LANG}_OPTIONS NATIVE_COMMAND "${OpenACC_${LANG}_FLAGS}")
    endif()
    _OPENACC_GET_SPEC_DATE("${LANG}" OpenACC_${LANG}_SPEC_DATE)
    _OPENACC_SET_VERSION_BY_SPEC_DATE("${LANG}")

    find_package_handle_standard_args(OpenACC_${LANG}
      NAME_MISMATCHED
      REQUIRED_VARS OpenACC_${LANG}_FLAGS
      VERSION_VAR OpenACC_${LANG}_VERSION
    )
  endif()
endforeach()

foreach (LANG IN ITEMS C CXX Fortran)
  if(OpenACC_${LANG}_FOUND AND NOT TARGET OpenACC::OpenACC_${LANG})
    add_library(OpenACC::OpenACC_${LANG} INTERFACE IMPORTED)
  endif()
  if(OpenACC_${LANG}_LIBRARIES)
    set_property(TARGET OpenACC::OpenACC_${LANG} PROPERTY
      INTERFACE_LINK_LIBRARIES "${OpenACC_${LANG}_LIBRARIES}")
  endif()
  if(OpenACC_${LANG}_FLAGS)
    set_property(TARGET OpenACC::OpenACC_${LANG} PROPERTY
      INTERFACE_COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:${LANG}>:${OpenACC_${LANG}_OPTIONS}>")
    set_property(TARGET OpenACC::OpenACC_${LANG} PROPERTY
      INTERFACE_LINK_OPTIONS "$<$<COMPILE_LANGUAGE:${LANG}>:${OpenACC_${LANG}_OPTIONS}>")
    unset(_OpenACC_${LANG}_OPTIONS)
  endif()
endforeach()

unset(OpenACC_C_CXX_TEST_SOURCE)
unset(OpenACC_Fortran_TEST_SOURCE)
unset(OpenACC_C_CXX_CHECK_VERSION_SOURCE)
unset(OpenACC_Fortran_CHECK_VERSION_SOURCE)
