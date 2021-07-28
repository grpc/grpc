# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindDevIL
---------



This module locates the developer's image library.
http://openil.sourceforge.net/

This module sets:

::

   IL_LIBRARIES -   the name of the IL library. These include the full path to
                    the core DevIL library. This one has to be linked into the
                    application.
   ILU_LIBRARIES -  the name of the ILU library. Again, the full path. This
                    library is for filters and effects, not actual loading. It
                    doesn't have to be linked if the functionality it provides
                    is not used.
   ILUT_LIBRARIES - the name of the ILUT library. Full path. This part of the
                    library interfaces with OpenGL. It is not strictly needed
                    in applications.
   IL_INCLUDE_DIR - where to find the il.h, ilu.h and ilut.h files.
   DevIL_FOUND    - this is set to TRUE if all the above variables were set.
                    This will be set to false if ILU or ILUT are not found,
                    even if they are not needed. In most systems, if one
                    library is found all the others are as well. That's the
                    way the DevIL developers release it.
#]=======================================================================]

# TODO: Add version support.
# Tested under Linux and Windows (MSVC)

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)

find_path(IL_INCLUDE_DIR il.h
  PATH_SUFFIXES include IL
  DOC "The path to the directory that contains il.h"
)

#message("IL_INCLUDE_DIR is ${IL_INCLUDE_DIR}")

find_library(IL_LIBRARIES
  NAMES IL DEVIL
  PATH_SUFFIXES libx32 lib64 lib lib32
  DOC "The file that corresponds to the base il library."
)

#message("IL_LIBRARIES is ${IL_LIBRARIES}")

find_library(ILUT_LIBRARIES
  NAMES ILUT
  PATH_SUFFIXES libx32 lib64 lib lib32
  DOC "The file that corresponds to the il (system?) utility library."
)

#message("ILUT_LIBRARIES is ${ILUT_LIBRARIES}")

find_library(ILU_LIBRARIES
  NAMES ILU
  PATH_SUFFIXES libx32 lib64 lib lib32
  DOC "The file that corresponds to the il utility library."
)

#message("ILU_LIBRARIES is ${ILU_LIBRARIES}")

FIND_PACKAGE_HANDLE_STANDARD_ARGS(DevIL DEFAULT_MSG
                                  IL_LIBRARIES ILU_LIBRARIES
                                  IL_INCLUDE_DIR)
# provide legacy variable for compatibility
set(IL_FOUND ${DevIL_FOUND})
