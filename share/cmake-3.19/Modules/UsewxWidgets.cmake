# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
UsewxWidgets
------------

Convenience include for using wxWidgets library.

Determines if wxWidgets was FOUND and sets the appropriate libs,
incdirs, flags, etc.  INCLUDE_DIRECTORIES and LINK_DIRECTORIES are
called.

USAGE

::

  # Note that for MinGW users the order of libs is important!
  find_package(wxWidgets REQUIRED net gl core base)
  include(${wxWidgets_USE_FILE})
  # and for each of your dependent executable/library targets:
  target_link_libraries(<YourTarget> ${wxWidgets_LIBRARIES})



DEPRECATED

::

  LINK_LIBRARIES is not called in favor of adding dependencies per target.



AUTHOR

::

  Jan Woetzel <jw -at- mip.informatik.uni-kiel.de>
#]=======================================================================]

# debug message and logging.
# comment these out for distribution
if    (NOT LOGFILE )
  #  set(LOGFILE "${PROJECT_BINARY_DIR}/CMakeOutput.log")
endif ()
macro(MSG _MSG)
  #  file(APPEND ${LOGFILE} "${CMAKE_CURRENT_LIST_FILE}(${CMAKE_CURRENT_LIST_LINE}):   ${_MSG}\n")
  #  message(STATUS "${CMAKE_CURRENT_LIST_FILE}(${CMAKE_CURRENT_LIST_LINE}): ${_MSG}")
endmacro()


MSG("wxWidgets_FOUND=${wxWidgets_FOUND}")
if   (wxWidgets_FOUND)
  if   (wxWidgets_INCLUDE_DIRS)
    if(wxWidgets_INCLUDE_DIRS_NO_SYSTEM)
      include_directories(${wxWidgets_INCLUDE_DIRS})
    else()
      include_directories(SYSTEM ${wxWidgets_INCLUDE_DIRS})
    endif()
    MSG("wxWidgets_INCLUDE_DIRS=${wxWidgets_INCLUDE_DIRS}")
  endif()

  if   (wxWidgets_LIBRARY_DIRS)
    link_directories(${wxWidgets_LIBRARY_DIRS})
    MSG("wxWidgets_LIBRARY_DIRS=${wxWidgets_LIBRARY_DIRS}")
  endif()

  if   (wxWidgets_DEFINITIONS)
    set_property(DIRECTORY APPEND
      PROPERTY COMPILE_DEFINITIONS ${wxWidgets_DEFINITIONS})
    MSG("wxWidgets_DEFINITIONS=${wxWidgets_DEFINITIONS}")
  endif()

  if   (wxWidgets_DEFINITIONS_DEBUG)
    set_property(DIRECTORY APPEND
      PROPERTY COMPILE_DEFINITIONS_DEBUG ${wxWidgets_DEFINITIONS_DEBUG})
    MSG("wxWidgets_DEFINITIONS_DEBUG=${wxWidgets_DEFINITIONS_DEBUG}")
  endif()

  if   (wxWidgets_CXX_FLAGS)
    # Flags are expected to be a string here, not a list.
    string(REPLACE ";" " " wxWidgets_CXX_FLAGS_str "${wxWidgets_CXX_FLAGS}")
    string(APPEND CMAKE_CXX_FLAGS " ${wxWidgets_CXX_FLAGS_str}")
    MSG("wxWidgets_CXX_FLAGS=${wxWidgets_CXX_FLAGS_str}")
    unset(wxWidgets_CXX_FLAGS_str)
  endif()

  # DEPRECATED JW
  # just for backward compatibility: add deps to all targets
  # library projects better use advanced find_package(wxWidgets) directly.
  #if(wxWidgets_LIBRARIES)
  #  link_libraries(${wxWidgets_LIBRARIES})
  #  # BUG: str too long:  MSG("wxWidgets_LIBRARIES=${wxWidgets_LIBRARIES}")
  #  if(LOGFILE)
  #    file(APPEND ${LOGFILE} "${CMAKE_CURRENT_LIST_FILE}(${CMAKE_CURRENT_LIST_LINE}):   ${wxWidgets_LIBRARIES}\n")
  #  endif()
  #endif()

else ()
  message("wxWidgets requested but not found.")
endif()
