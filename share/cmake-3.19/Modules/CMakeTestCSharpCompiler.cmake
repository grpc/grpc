# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

if(CMAKE_CSharp_COMPILER_FORCED)
  # The compiler configuration was forced by the user.
  # Assume the user has configured all compiler information.
  set(CMAKE_CSharp_COMPILER_WORKS TRUE)
  return()
endif()

include(CMakeTestCompilerCommon)

unset(CMAKE_CSharp_COMPILER_WORKS CACHE)

set(test_compile_file "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp/testCSharpCompiler.cs")

# This file is used by EnableLanguage in cmGlobalGenerator to
# determine that the selected C# compiler can actually compile
# and link the most basic of programs. If not, a fatal error
# is set and cmake stops processing commands and will not generate
# any makefiles or projects.
if(NOT CMAKE_CSharp_COMPILER_WORKS)
  # Don't call PrintTestCompilerStatus() because the "C#" we want to pass
  # as the LANG doesn't match with the variable name "CMAKE_CSharp_COMPILER"
  message(CHECK_START "Check for working C# compiler: ${CMAKE_CSharp_COMPILER}")
  file(WRITE "${test_compile_file}"
    "namespace Test {"
    "   public class CSharp {"
    "       static void Main(string[] args) {}"
    "   }"
    "}"
    )
  try_compile(CMAKE_CSharp_COMPILER_WORKS ${CMAKE_BINARY_DIR} "${test_compile_file}"
    OUTPUT_VARIABLE __CMAKE_CSharp_COMPILER_OUTPUT
    )
  # Move result from cache to normal variable.
  set(CMAKE_CSharp_COMPILER_WORKS ${CMAKE_CSharp_COMPILER_WORKS})
  unset(CMAKE_CSharp_COMPILER_WORKS CACHE)
  set(CSharp_TEST_WAS_RUN 1)
endif()

if(NOT CMAKE_CSharp_COMPILER_WORKS)
  PrintTestCompilerResult(CHECK_FAIL "broken")
  file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
    "Determining if the C# compiler works failed with "
    "the following output:\n${__CMAKE_CSharp_COMPILER_OUTPUT}\n\n")
  string(REPLACE "\n" "\n  " _output "${__CMAKE_CSharp_COMPILER_OUTPUT}")
  message(FATAL_ERROR "The C# compiler\n  \"${CMAKE_CSharp_COMPILER}\"\n"
    "is not able to compile a simple test program.\nIt fails "
    "with the following output:\n  ${_output}\n\n"
    "CMake will not be able to correctly generate this project.")
else()
  if(CSharp_TEST_WAS_RUN)
    PrintTestCompilerResult(CHECK_PASS "works")
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeOutput.log
      "Determining if the C# compiler works passed with "
      "the following output:\n${__CMAKE_CSharp_COMPILER_OUTPUT}\n\n")
  endif()

  # Re-configure to save learned information.
  configure_file(
    ${CMAKE_ROOT}/Modules/CMakeCSharpCompiler.cmake.in
    ${CMAKE_PLATFORM_INFO_DIR}/CMakeCSharpCompiler.cmake
    @ONLY
    )
  include(${CMAKE_PLATFORM_INFO_DIR}/CMakeCSharpCompiler.cmake)
endif()
