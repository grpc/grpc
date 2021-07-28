# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindOpenSceneGraph
------------------

Find OpenSceneGraph (3D graphics application programming interface)

This module searches for the OpenSceneGraph core "osg" library as well
as :module:`FindOpenThreads`, and whatever additional ``COMPONENTS``
(nodekits) that you specify.

::

    See http://www.openscenegraph.org



NOTE: To use this module effectively you must either require ``CMake >=
2.6.3`` with  :command:`cmake_minimum_required(VERSION 2.6.3)` or download
and place :module:`FindOpenThreads`, :module:`Findosg` functions,
:module:`Findosg` and ``Find<etc>.cmake`` files into your
:variable:`CMAKE_MODULE_PATH`.

==================================

This module accepts the following variables (note mixed case)

::

    OpenSceneGraph_DEBUG - Enable debugging output



::

    OpenSceneGraph_MARK_AS_ADVANCED - Mark cache variables as advanced
                                      automatically



The following environment variables are also respected for finding the
OSG and it's various components.  :variable:`CMAKE_PREFIX_PATH` can also be
used for this (see :command:`find_library` CMake documentation).

``<MODULE>_DIR``
  (where ``MODULE`` is of the form "OSGVOLUME" and there is
  a :module:`FindosgVolume`.cmake` file)
``OSG_DIR``
  ..
``OSGDIR``
  ..
``OSG_ROOT``
  ..


[CMake 2.8.10]: The CMake variable ``OSG_DIR`` can now be used as well to
influence detection, instead of needing to specify an environment
variable.

This module defines the following output variables:

::

    OPENSCENEGRAPH_FOUND - Was the OSG and all of the specified components found?



::

    OPENSCENEGRAPH_VERSION - The version of the OSG which was found



::

    OPENSCENEGRAPH_INCLUDE_DIRS - Where to find the headers



::

    OPENSCENEGRAPH_LIBRARIES - The OSG libraries



================================== Example Usage:

::

  find_package(OpenSceneGraph 2.0.0 REQUIRED osgDB osgUtil)
      # libOpenThreads & libosg automatically searched
  include_directories(${OPENSCENEGRAPH_INCLUDE_DIRS})



::

  add_executable(foo foo.cc)
  target_link_libraries(foo ${OPENSCENEGRAPH_LIBRARIES})
#]=======================================================================]

#
# Naming convention:
#  Local variables of the form _osg_foo
#  Input variables of the form OpenSceneGraph_FOO
#  Output variables of the form OPENSCENEGRAPH_FOO
#

include(${CMAKE_CURRENT_LIST_DIR}/Findosg_functions.cmake)

set(_osg_modules_to_process)
foreach(_osg_component ${OpenSceneGraph_FIND_COMPONENTS})
    list(APPEND _osg_modules_to_process ${_osg_component})
endforeach()
list(APPEND _osg_modules_to_process "osg" "OpenThreads")
list(REMOVE_DUPLICATES _osg_modules_to_process)

if(OpenSceneGraph_DEBUG)
    message(STATUS "[ FindOpenSceneGraph.cmake:${CMAKE_CURRENT_LIST_LINE} ] "
        "Components = ${_osg_modules_to_process}")
endif()

#
# First we need to find and parse osg/Version
#
OSG_FIND_PATH(OSG osg/Version)
if(OpenSceneGraph_MARK_AS_ADVANCED)
    OSG_MARK_AS_ADVANCED(OSG)
endif()

# Try to ascertain the version...
if(OSG_INCLUDE_DIR)
    if(OpenSceneGraph_DEBUG)
        message(STATUS "[ FindOpenSceneGraph.cmake:${CMAKE_CURRENT_LIST_LINE} ] "
            "Detected OSG_INCLUDE_DIR = ${OSG_INCLUDE_DIR}")
    endif()

    set(_osg_Version_file "${OSG_INCLUDE_DIR}/osg/Version")
    if("${OSG_INCLUDE_DIR}" MATCHES "\\.framework$" AND NOT EXISTS "${_osg_Version_file}")
        set(_osg_Version_file "${OSG_INCLUDE_DIR}/Headers/Version")
    endif()

    if(EXISTS "${_osg_Version_file}")
      file(STRINGS "${_osg_Version_file}" _osg_Version_contents
           REGEX "#define (OSG_VERSION_[A-Z]+|OPENSCENEGRAPH_[A-Z]+_VERSION)[ \t]+[0-9]+")
    else()
      set(_osg_Version_contents "unknown")
    endif()

    string(REGEX MATCH ".*#define OSG_VERSION_MAJOR[ \t]+[0-9]+.*"
        _osg_old_defines "${_osg_Version_contents}")
    string(REGEX MATCH ".*#define OPENSCENEGRAPH_MAJOR_VERSION[ \t]+[0-9]+.*"
        _osg_new_defines "${_osg_Version_contents}")
    if(_osg_old_defines)
        string(REGEX REPLACE ".*#define OSG_VERSION_MAJOR[ \t]+([0-9]+).*"
            "\\1" _osg_VERSION_MAJOR ${_osg_Version_contents})
        string(REGEX REPLACE ".*#define OSG_VERSION_MINOR[ \t]+([0-9]+).*"
            "\\1" _osg_VERSION_MINOR ${_osg_Version_contents})
        string(REGEX REPLACE ".*#define OSG_VERSION_PATCH[ \t]+([0-9]+).*"
            "\\1" _osg_VERSION_PATCH ${_osg_Version_contents})
    elseif(_osg_new_defines)
        string(REGEX REPLACE ".*#define OPENSCENEGRAPH_MAJOR_VERSION[ \t]+([0-9]+).*"
            "\\1" _osg_VERSION_MAJOR ${_osg_Version_contents})
        string(REGEX REPLACE ".*#define OPENSCENEGRAPH_MINOR_VERSION[ \t]+([0-9]+).*"
            "\\1" _osg_VERSION_MINOR ${_osg_Version_contents})
        string(REGEX REPLACE ".*#define OPENSCENEGRAPH_PATCH_VERSION[ \t]+([0-9]+).*"
            "\\1" _osg_VERSION_PATCH ${_osg_Version_contents})
    else()
        message(WARNING "[ FindOpenSceneGraph.cmake:${CMAKE_CURRENT_LIST_LINE} ] "
            "Failed to parse version number, please report this as a bug")
    endif()
    unset(_osg_Version_contents)

    set(OPENSCENEGRAPH_VERSION "${_osg_VERSION_MAJOR}.${_osg_VERSION_MINOR}.${_osg_VERSION_PATCH}"
                                CACHE INTERNAL "The version of OSG which was detected")
    if(OpenSceneGraph_DEBUG)
        message(STATUS "[ FindOpenSceneGraph.cmake:${CMAKE_CURRENT_LIST_LINE} ] "
            "Detected version ${OPENSCENEGRAPH_VERSION}")
    endif()
endif()

set(_osg_quiet)
if(OpenSceneGraph_FIND_QUIETLY)
    set(_osg_quiet "QUIET")
endif()
#
# Here we call find_package() on all of the components
#
foreach(_osg_module ${_osg_modules_to_process})
    if(OpenSceneGraph_DEBUG)
        message(STATUS "[ FindOpenSceneGraph.cmake:${CMAKE_CURRENT_LIST_LINE} ] "
            "Calling find_package(${_osg_module} ${_osg_required} ${_osg_quiet})")
    endif()
    find_package(${_osg_module} ${_osg_quiet})

    string(TOUPPER ${_osg_module} _osg_module_UC)
    # append to list if module was found OR is required
    if( ${_osg_module_UC}_FOUND OR OpenSceneGraph_FIND_REQUIRED )
      list(APPEND OPENSCENEGRAPH_INCLUDE_DIR ${${_osg_module_UC}_INCLUDE_DIR})
      list(APPEND OPENSCENEGRAPH_LIBRARIES ${${_osg_module_UC}_LIBRARIES})
    endif()

    if(OpenSceneGraph_MARK_AS_ADVANCED)
        OSG_MARK_AS_ADVANCED(${_osg_module})
    endif()
endforeach()

if(OPENSCENEGRAPH_INCLUDE_DIR)
    list(REMOVE_DUPLICATES OPENSCENEGRAPH_INCLUDE_DIR)
endif()

#
# Check each module to see if it's found
#
set(_osg_component_founds)
if(OpenSceneGraph_FIND_REQUIRED)
    foreach(_osg_module ${_osg_modules_to_process})
        string(TOUPPER ${_osg_module} _osg_module_UC)
        list(APPEND _osg_component_founds ${_osg_module_UC}_FOUND)
    endforeach()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(OpenSceneGraph
                                  REQUIRED_VARS OPENSCENEGRAPH_LIBRARIES OPENSCENEGRAPH_INCLUDE_DIR ${_osg_component_founds}
                                  VERSION_VAR OPENSCENEGRAPH_VERSION)

unset(_osg_component_founds)

set(OPENSCENEGRAPH_INCLUDE_DIRS ${OPENSCENEGRAPH_INCLUDE_DIR})
