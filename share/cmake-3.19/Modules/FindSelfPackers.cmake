# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindSelfPackers
---------------

Find upx

This module looks for some executable packers (i.e.  software that
compress executables or shared libs into on-the-fly self-extracting
executables or shared libs.  Examples:

::

  UPX: http://wildsau.idv.uni-linz.ac.at/mfx/upx.html
#]=======================================================================]

include(${CMAKE_CURRENT_LIST_DIR}/FindCygwin.cmake)

find_program(SELF_PACKER_FOR_EXECUTABLE
  upx
  ${CYGWIN_INSTALL_PATH}/bin
)

find_program(SELF_PACKER_FOR_SHARED_LIB
  upx
  ${CYGWIN_INSTALL_PATH}/bin
)

mark_as_advanced(
  SELF_PACKER_FOR_EXECUTABLE
  SELF_PACKER_FOR_SHARED_LIB
)

#
# Set flags
#
if (SELF_PACKER_FOR_EXECUTABLE MATCHES "upx")
  set (SELF_PACKER_FOR_EXECUTABLE_FLAGS "-q" CACHE STRING
       "Flags for the executable self-packer.")
else ()
  set (SELF_PACKER_FOR_EXECUTABLE_FLAGS "" CACHE STRING
       "Flags for the executable self-packer.")
endif ()

if (SELF_PACKER_FOR_SHARED_LIB MATCHES "upx")
  set (SELF_PACKER_FOR_SHARED_LIB_FLAGS "-q" CACHE STRING
       "Flags for the shared lib self-packer.")
else ()
  set (SELF_PACKER_FOR_SHARED_LIB_FLAGS "" CACHE STRING
       "Flags for the shared lib self-packer.")
endif ()

mark_as_advanced(
  SELF_PACKER_FOR_EXECUTABLE_FLAGS
  SELF_PACKER_FOR_SHARED_LIB_FLAGS
)
