# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindosgText
-----------



This is part of the Findosg* suite used to find OpenSceneGraph
components.  Each component is separate and you must opt in to each
module.  You must also opt into OpenGL and OpenThreads (and Producer
if needed) as these modules won't do it for you.  This is to allow you
control over your own system piece by piece in case you need to opt
out of certain components or change the Find behavior for a particular
module (perhaps because the default FindOpenGL.cmake module doesn't
work with your system as an example).  If you want to use a more
convenient module that includes everything, use the
FindOpenSceneGraph.cmake instead of the Findosg*.cmake modules.

Locate osgText This module defines

OSGTEXT_FOUND - Was osgText found? OSGTEXT_INCLUDE_DIR - Where to find
the headers OSGTEXT_LIBRARIES - The libraries to link for osgText (use
this)

OSGTEXT_LIBRARY - The osgText library OSGTEXT_LIBRARY_DEBUG - The
osgText debug library

$OSGDIR is an environment variable that would correspond to the
./configure --prefix=$OSGDIR used in building osg.

Created by Eric Wing.
#]=======================================================================]

# Header files are presumed to be included like
# #include <osg/PositionAttitudeTransform>
# #include <osgText/Text>

include(${CMAKE_CURRENT_LIST_DIR}/Findosg_functions.cmake)
OSG_FIND_PATH   (OSGTEXT osgText/Text)
OSG_FIND_LIBRARY(OSGTEXT osgText)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(osgText DEFAULT_MSG
    OSGTEXT_LIBRARY OSGTEXT_INCLUDE_DIR)
