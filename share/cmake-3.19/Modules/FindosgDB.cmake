# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindosgDB
---------



This is part of the ``Findosg*`` suite used to find OpenSceneGraph
components.  Each component is separate and you must opt in to each
module.  You must also opt into OpenGL and OpenThreads (and Producer
if needed) as these modules won't do it for you.  This is to allow you
control over your own system piece by piece in case you need to opt
out of certain components or change the Find behavior for a particular
module (perhaps because the default :module:`FindOpenGL` module doesn't
work with your system as an example).  If you want to use a more
convenient module that includes everything, use the
:module:`FindOpenSceneGraph` instead of the ``Findosg*.cmake`` modules.

Locate osgDB This module defines:

``OSGDB_FOUND``
  Was osgDB found?

``OSGDB_INCLUDE_DIR``
  Where to find the headers

``OSGDB_LIBRARIES``
  The libraries to link against for the osgDB

``OSGDB_LIBRARY``
  The osgDB library

``OSGDB_LIBRARY_DEBUG``
  The osgDB debug library

``$OSGDIR`` is an environment variable that would correspond to::

  ./configure --prefix=$OSGDIR used in building osg.

#]=======================================================================]

# Header files are presumed to be included like
# #include <osg/PositionAttitudeTransform>
# #include <osgDB/DatabasePager>

include(${CMAKE_CURRENT_LIST_DIR}/Findosg_functions.cmake)
OSG_FIND_PATH   (OSGDB osgDB/DatabasePager)
OSG_FIND_LIBRARY(OSGDB osgDB)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(osgDB DEFAULT_MSG
    OSGDB_LIBRARY OSGDB_INCLUDE_DIR)
