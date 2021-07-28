# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindProducer
------------



Though Producer isn't directly part of OpenSceneGraph, its primary
user is OSG so I consider this part of the Findosg* suite used to find
OpenSceneGraph components.  You'll notice that I accept OSGDIR as an
environment path.

Each component is separate and you must opt in to each module.  You
must also opt into OpenGL (and OpenThreads?) as these modules won't do
it for you.  This is to allow you control over your own system piece
by piece in case you need to opt out of certain components or change
the Find behavior for a particular module (perhaps because the default
FindOpenGL.cmake module doesn't work with your system as an example).
If you want to use a more convenient module that includes everything,
use the FindOpenSceneGraph.cmake instead of the Findosg*.cmake
modules.

Locate Producer This module defines PRODUCER_LIBRARY PRODUCER_FOUND,
if false, do not try to link to Producer PRODUCER_INCLUDE_DIR, where
to find the headers

$PRODUCER_DIR is an environment variable that would correspond to the
./configure --prefix=$PRODUCER_DIR used in building osg.

Created by Eric Wing.
#]=======================================================================]

# Header files are presumed to be included like
# #include <Producer/CameraGroup>

# Try the user's environment request before anything else.
find_path(PRODUCER_INCLUDE_DIR Producer/CameraGroup
  HINTS
    ENV PRODUCER_DIR
    ENV OSG_DIR
    ENV OSGDIR
  PATH_SUFFIXES include
  PATHS
    ~/Library/Frameworks
    /Library/Frameworks
    /opt
    [HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session\ Manager\\Environment;OpenThreads_ROOT]
    [HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session\ Manager\\Environment;OSG_ROOT]
)

find_library(PRODUCER_LIBRARY
  NAMES Producer
  HINTS
    ENV PRODUCER_DIR
    ENV OSG_DIR
    ENV OSGDIR
  PATH_SUFFIXES lib
  PATHS
  /opt
)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Producer DEFAULT_MSG
    PRODUCER_LIBRARY PRODUCER_INCLUDE_DIR)
