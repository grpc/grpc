# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CMakeFindFrameworks
-------------------

helper module to find OSX frameworks

This module reads hints about search locations from variables::

  CMAKE_FIND_FRAMEWORK_EXTRA_LOCATIONS - Extra directories
#]=======================================================================]

if(NOT CMAKE_FIND_FRAMEWORKS_INCLUDED)
  set(CMAKE_FIND_FRAMEWORKS_INCLUDED 1)
  macro(CMAKE_FIND_FRAMEWORKS fwk)
    set(${fwk}_FRAMEWORKS)
    if(APPLE)
      file(TO_CMAKE_PATH "$ENV{CMAKE_FRAMEWORK_PATH}" _cmff_CMAKE_FRAMEWORK_PATH)
      set(_cmff_search_paths
            ${CMAKE_FRAMEWORK_PATH}
            ${_cmff_CMAKE_FRAMEWORK_PATH}
            ~/Library/Frameworks
            /usr/local/Frameworks
            /Library/Frameworks
            /System/Library/Frameworks
            /Network/Library/Frameworks
            ${CMAKE_SYSTEM_FRAMEWORK_PATH})

      # For backwards compatibility reasons,
      # CMAKE_FIND_FRAMEWORK_EXTRA_LOCATIONS includes ${fwk}.framework
      list(TRANSFORM _cmff_search_paths APPEND /${fwk}.framework)
      list(APPEND _cmff_search_paths ${CMAKE_FIND_FRAMEWORK_EXTRA_LOCATIONS})

      list(REMOVE_DUPLICATES _cmff_search_paths)

      foreach(dir IN LISTS _cmff_search_paths)
        if(EXISTS ${dir})
          set(${fwk}_FRAMEWORKS ${${fwk}_FRAMEWORKS} ${dir})
        endif()
      endforeach()
    endif()
  endmacro()
endif()
