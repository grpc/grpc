# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


# This module is shared by multiple languages; use include blocker.
if(__WINDOWS_INTEL)
  return()
endif()
set(__WINDOWS_INTEL 1)

include(Platform/Windows-MSVC)
macro(__windows_compiler_intel lang)
  __windows_compiler_msvc(${lang})
  string(REPLACE "<CMAKE_AR>" "xilib" CMAKE_${lang}_CREATE_STATIC_LIBRARY "${CMAKE_${lang}_CREATE_STATIC_LIBRARY}")
  foreach(rule CREATE_SHARED_LIBRARY CREATE_SHARED_MODULE LINK_EXECUTABLE)
    string(REPLACE "<CMAKE_LINKER>" "xilink" CMAKE_${lang}_${rule} "${CMAKE_${lang}_${rule}}")
  endforeach()
endmacro()
