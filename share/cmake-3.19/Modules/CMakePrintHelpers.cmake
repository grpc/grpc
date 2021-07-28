# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CMakePrintHelpers
-----------------

Convenience functions for printing properties and variables, useful
e.g. for debugging.

::

  cmake_print_properties([TARGETS target1 ..  targetN]
                        [SOURCES source1 .. sourceN]
                        [DIRECTORIES dir1 .. dirN]
                        [TESTS test1 .. testN]
                        [CACHE_ENTRIES entry1 .. entryN]
                        PROPERTIES prop1 .. propN )

This function prints the values of the properties of the given targets,
source files, directories, tests or cache entries.  Exactly one of the
scope keywords must be used.  Example::

  cmake_print_properties(TARGETS foo bar PROPERTIES
                         LOCATION INTERFACE_INCLUDE_DIRECTORIES)

This will print the LOCATION and INTERFACE_INCLUDE_DIRECTORIES properties for
both targets foo and bar.

::

  cmake_print_variables(var1 var2 ..  varN)

This function will print the name of each variable followed by its value.
Example::

  cmake_print_variables(CMAKE_C_COMPILER CMAKE_MAJOR_VERSION DOES_NOT_EXIST)

Gives::

  -- CMAKE_C_COMPILER="/usr/bin/gcc" ; CMAKE_MAJOR_VERSION="2" ; DOES_NOT_EXIST=""
#]=======================================================================]

function(cmake_print_variables)
  set(msg "")
  foreach(var ${ARGN})
    if(msg)
      string(APPEND msg " ; ")
    endif()
    string(APPEND msg "${var}=\"${${var}}\"")
  endforeach()
  message(STATUS "${msg}")
endfunction()


function(cmake_print_properties)
  set(options )
  set(oneValueArgs )
  set(multiValueArgs TARGETS SOURCES TESTS DIRECTORIES CACHE_ENTRIES PROPERTIES )

  cmake_parse_arguments(CPP "${options}" "${oneValueArgs}" "${multiValueArgs}"  ${ARGN})

  if(CPP_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Unknown keywords given to cmake_print_properties(): \"${CPP_UNPARSED_ARGUMENTS}\"")
    return()
  endif()

  if(NOT CPP_PROPERTIES)
    message(FATAL_ERROR "Required argument PROPERTIES missing in cmake_print_properties() call")
    return()
  endif()

  set(mode)
  set(items)
  set(keyword)

  if(CPP_TARGETS)
    set(items ${CPP_TARGETS})
    set(mode ${mode} TARGETS)
    set(keyword TARGET)
  endif()

  if(CPP_SOURCES)
    set(items ${CPP_SOURCES})
    set(mode ${mode} SOURCES)
    set(keyword SOURCE)
  endif()

  if(CPP_TESTS)
    set(items ${CPP_TESTS})
    set(mode ${mode} TESTS)
    set(keyword TEST)
  endif()

  if(CPP_DIRECTORIES)
    set(items ${CPP_DIRECTORIES})
    set(mode ${mode} DIRECTORIES)
    set(keyword DIRECTORY)
  endif()

  if(CPP_CACHE_ENTRIES)
    set(items ${CPP_CACHE_ENTRIES})
    set(mode ${mode} CACHE_ENTRIES)
    set(keyword CACHE)
  endif()

  if(NOT mode)
    message(FATAL_ERROR "Mode keyword missing in cmake_print_properties() call, must be one of TARGETS SOURCES TESTS DIRECTORIES CACHE_ENTRIES PROPERTIES")
    return()
  endif()

  list(LENGTH mode modeLength)
  if("${modeLength}" GREATER 1)
    message(FATAL_ERROR "Multiple mode keyword used in cmake_print_properties() call, it must be exactly one of TARGETS SOURCES TESTS DIRECTORIES CACHE_ENTRIES PROPERTIES")
    return()
  endif()

  set(msg "\n")
  foreach(item ${items})

    set(itemExists TRUE)
    if(keyword STREQUAL "TARGET")
      if(NOT TARGET ${item})
      set(itemExists FALSE)
      string(APPEND msg "\n No such TARGET \"${item}\" !\n\n")
      endif()
    endif()

    if (itemExists)
      string(APPEND msg " Properties for ${keyword} ${item}:\n")
      foreach(prop ${CPP_PROPERTIES})

        get_property(propertySet ${keyword} ${item} PROPERTY "${prop}" SET)

        if(propertySet)
          get_property(property ${keyword} ${item} PROPERTY "${prop}")
          string(APPEND msg "   ${item}.${prop} = \"${property}\"\n")
        else()
          string(APPEND msg "   ${item}.${prop} = <NOTFOUND>\n")
        endif()
      endforeach()
    endif()

  endforeach()
  message(STATUS "${msg}")

endfunction()
