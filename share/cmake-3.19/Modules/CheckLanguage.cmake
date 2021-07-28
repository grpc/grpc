# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CheckLanguage
-------------

Check if a language can be enabled

Usage:

::

  check_language(<lang>)

where ``<lang>`` is a language that may be passed to :command:`enable_language`
such as ``Fortran``.  If :variable:`CMAKE_<LANG>_COMPILER` is already defined
the check does nothing.  Otherwise it tries enabling the language in a
test project.  The result is cached in :variable:`CMAKE_<LANG>_COMPILER`
as the compiler that was found, or ``NOTFOUND`` if the language cannot be
enabled. For CUDA which can have an explicit host compiler, the cache
:variable:`CMAKE_CUDA_HOST_COMPILER` variable will be set if it was required
for compilation (and cleared if it was not).

Example:

::

  check_language(Fortran)
  if(CMAKE_Fortran_COMPILER)
    enable_language(Fortran)
  else()
    message(STATUS "No Fortran support")
  endif()
#]=======================================================================]

include_guard(GLOBAL)

macro(check_language lang)
  if(NOT DEFINED CMAKE_${lang}_COMPILER)
    set(_desc "Looking for a ${lang} compiler")
    message(CHECK_START "${_desc}")
    file(REMOVE_RECURSE ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/Check${lang})

    set(extra_compiler_variables)
    if(${lang} STREQUAL CUDA)
      set(extra_compiler_variables "set(CMAKE_CUDA_HOST_COMPILER \\\"\${CMAKE_CUDA_HOST_COMPILER}\\\")")
    endif()

    set(_cl_content
      "cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(Check${lang} ${lang})
file(WRITE \"\${CMAKE_CURRENT_BINARY_DIR}/result.cmake\"
  \"set(CMAKE_${lang}_COMPILER \\\"\${CMAKE_${lang}_COMPILER}\\\")\\n\"
  \"${extra_compiler_variables}\\n\"
  )"
    )

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/Check${lang}/CMakeLists.txt"
      "${_cl_content}")
    if(CMAKE_GENERATOR_INSTANCE)
      set(_D_CMAKE_GENERATOR_INSTANCE "-DCMAKE_GENERATOR_INSTANCE:INTERNAL=${CMAKE_GENERATOR_INSTANCE}")
    else()
      set(_D_CMAKE_GENERATOR_INSTANCE "")
    endif()
    if(CMAKE_GENERATOR MATCHES "^(Xcode$|Green Hills MULTI$|Visual Studio)")
      set(_D_CMAKE_MAKE_PROGRAM "")
    else()
      set(_D_CMAKE_MAKE_PROGRAM "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}")
    endif()
    execute_process(
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/Check${lang}
      COMMAND ${CMAKE_COMMAND} . -G ${CMAKE_GENERATOR}
                                 -A "${CMAKE_GENERATOR_PLATFORM}"
                                 -T "${CMAKE_GENERATOR_TOOLSET}"
                                 ${_D_CMAKE_GENERATOR_INSTANCE}
                                 ${_D_CMAKE_MAKE_PROGRAM}
      OUTPUT_VARIABLE _cl_output
      ERROR_VARIABLE _cl_output
      RESULT_VARIABLE _cl_result
      )
    include(${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/Check${lang}/result.cmake OPTIONAL)
    if(CMAKE_${lang}_COMPILER AND "${_cl_result}" STREQUAL "0")
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
        "${_desc} passed with the following output:\n"
        "${_cl_output}\n")
      set(_CHECK_COMPILER_STATUS CHECK_PASS)
    else()
      set(CMAKE_${lang}_COMPILER NOTFOUND)
      set(_CHECK_COMPILER_STATUS CHECK_FAIL)
      file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "${_desc} failed with the following output:\n"
        "${_cl_output}\n")
    endif()
    message(${_CHECK_COMPILER_STATUS} "${CMAKE_${lang}_COMPILER}")
    set(CMAKE_${lang}_COMPILER "${CMAKE_${lang}_COMPILER}" CACHE FILEPATH "${lang} compiler")
    mark_as_advanced(CMAKE_${lang}_COMPILER)

    if(CMAKE_${lang}_HOST_COMPILER)
      message(STATUS "Looking for a ${lang} host compiler - ${CMAKE_${lang}_HOST_COMPILER}")
      set(CMAKE_${lang}_HOST_COMPILER "${CMAKE_${lang}_HOST_COMPILER}" CACHE FILEPATH "${lang} host compiler")
      mark_as_advanced(CMAKE_${lang}_HOST_COMPILER)
    endif()

  endif()
endmacro()
