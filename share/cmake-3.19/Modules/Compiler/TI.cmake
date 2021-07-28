# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# This module is shared by multiple languages; use include blocker.
if(__COMPILER_TI)
  return()
endif()
set(__COMPILER_TI 1)

include(Compiler/CMakeCommonCompilerMacros)

set(__COMPILER_TI_SOURCE_FLAG_C   "--c_file")
set(__COMPILER_TI_SOURCE_FLAG_CXX "--cpp_file")
set(__COMPILER_TI_SOURCE_FLAG_ASM "--asm_file")

macro(__compiler_ti lang)
  set(CMAKE_${lang}_RESPONSE_FILE_FLAG "--cmd_file=")

  set(CMAKE_INCLUDE_FLAG_${lang} "--include_path=")
  set(CMAKE_DEPFILE_FLAGS_${lang} "--preproc_with_compile --preproc_dependency=<DEPFILE>")

  set(CMAKE_${lang}_CREATE_PREPROCESSED_SOURCE "<CMAKE_${lang}_COMPILER> --preproc_only ${__COMPILER_TI_SOURCE_FLAG_${lang}}=<SOURCE> <DEFINES> <INCLUDES> <FLAGS> --output_file=<PREPROCESSED_SOURCE>")
  set(CMAKE_${lang}_CREATE_ASSEMBLY_SOURCE     "<CMAKE_${lang}_COMPILER> --compile_only --skip_assembler ${__COMPILER_TI_SOURCE_FLAG_${lang}}=<SOURCE> <DEFINES> <INCLUDES> <FLAGS> --output_file=<ASSEMBLY_SOURCE>")

  set(CMAKE_${lang}_COMPILE_OBJECT  "<CMAKE_${lang}_COMPILER> --compile_only ${__COMPILER_TI_SOURCE_FLAG_${lang}}=<SOURCE> <DEFINES> <INCLUDES> <FLAGS> --output_file=<OBJECT>")

  set(CMAKE_${lang}_ARCHIVE_CREATE "<CMAKE_AR> qr <TARGET> <OBJECTS>")
  set(CMAKE_${lang}_ARCHIVE_APPEND "<CMAKE_AR> qa <TARGET> <OBJECTS>")
  set(CMAKE_${lang}_ARCHIVE_FINISH "")

  # After the --run_linker flag a response file is not possible
  set(CMAKE_${lang}_RESPONSE_FILE_LINK_FLAG "")
  set(CMAKE_${lang}_USE_RESPONSE_FILE_FOR_LIBRARIES 0)
  set(CMAKE_${lang}_USE_RESPONSE_FILE_FOR_OBJECTS 0)

  set(CMAKE_${lang}_LINK_EXECUTABLE "<CMAKE_${lang}_COMPILER> <FLAGS> --run_linker --output_file=<TARGET> --map_file=<TARGET_NAME>.map <CMAKE_${lang}_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> <LINK_LIBRARIES>")
endmacro()

set(CMAKE_LIBRARY_PATH_FLAG "--search_path=")
set(CMAKE_LINK_LIBRARY_FLAG "--library=")
