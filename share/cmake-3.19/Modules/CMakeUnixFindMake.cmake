# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


find_program(CMAKE_MAKE_PROGRAM NAMES gmake make smake)
mark_as_advanced(CMAKE_MAKE_PROGRAM)

# Look for a make tool provided by Xcode
if(NOT CMAKE_MAKE_PROGRAM AND CMAKE_HOST_APPLE)
  execute_process(COMMAND xcrun --find make
    OUTPUT_VARIABLE _xcrun_out OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE _xcrun_err)
  if(_xcrun_out)
    set_property(CACHE CMAKE_MAKE_PROGRAM PROPERTY VALUE "${_xcrun_out}")
  endif()
endif()
