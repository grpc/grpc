# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


include(Compiler/GNU)

macro(__compiler_qcc lang)
  __compiler_gnu(${lang})

  # http://www.qnx.com/developers/docs/6.4.0/neutrino/utilities/q/qcc.html#examples
  set(CMAKE_${lang}_COMPILE_OPTIONS_TARGET "-V")

  set(CMAKE_PREFIX_LIBRARY_ARCHITECTURE "ON")

  set(CMAKE_${lang}_COMPILE_OPTIONS_SYSROOT "-Wc,-isysroot,")
  set(CMAKE_INCLUDE_SYSTEM_FLAG_${lang} "-Wp,-isystem,")
  set(CMAKE_DEPFILE_FLAGS_${lang} "-Wp,-MD,<DEPFILE> -Wp,-MT,<OBJECT> -Wp,-MF,<DEPFILE>")

  set(CMAKE_${lang}_LINKER_WRAPPER_FLAG "-Wl,")
  set(CMAKE_${lang}_LINKER_WRAPPER_FLAG_SEP ",")

  set(_CMAKE_${lang}_IPO_SUPPORTED_BY_CMAKE NO)
  set(_CMAKE_${lang}_IPO_MAY_BE_SUPPORTED_BY_COMPILER NO)

  set(CMAKE_${lang}_COMPILER_PREDEFINES_COMMAND "${CMAKE_${lang}_COMPILER}")
  if(CMAKE_${lang}_COMPILER_ARG1)
    separate_arguments(_COMPILER_ARGS NATIVE_COMMAND "${CMAKE_${lang}_COMPILER_ARG1}")
    list(APPEND CMAKE_${lang}_COMPILER_PREDEFINES_COMMAND ${_COMPILER_ARGS})
    unset(_COMPILER_ARGS)
  endif()
  list(APPEND CMAKE_${lang}_COMPILER_PREDEFINES_COMMAND "-Wp,-dM" "-E" "-c" "${CMAKE_ROOT}/Modules/CMakeCXXCompilerABI.cpp")

  unset(CMAKE_${lang}_COMPILE_OPTIONS_IPO)
  unset(CMAKE_${lang}_ARCHIVE_CREATE_IPO)
  unset(CMAKE_${lang}_ARCHIVE_APPEND_IPO)
  unset(CMAKE_${lang}_ARCHIVE_FINISH_IPO)
endmacro()
