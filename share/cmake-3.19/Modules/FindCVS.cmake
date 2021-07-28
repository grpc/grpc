# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindCVS
-------

Find the Concurrent Versions System (CVS).

The module defines the following variables:

::

   CVS_EXECUTABLE - path to cvs command line client
   CVS_FOUND - true if the command line client was found

Example usage:

::

   find_package(CVS)
   if(CVS_FOUND)
     message("CVS found: ${CVS_EXECUTABLE}")
   endif()
#]=======================================================================]

# CVSNT

get_filename_component(
  CVSNT_TypeLib_Win32
  "[HKEY_CLASSES_ROOT\\TypeLib\\{2BDF7A65-0BFE-4B1A-9205-9AB900C7D0DA}\\1.0\\0\\win32]"
  PATH)

get_filename_component(
  CVSNT_Services_EventMessagePath
  "[HKEY_LOCAL_MACHINE\\SYSTEM\\ControlSet001\\Services\\Eventlog\\Application\\cvsnt;EventMessageFile]"
  PATH)

# WinCVS (in case CVSNT was installed in the same directory)

get_filename_component(
  WinCVS_Folder_Command
  "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\Folder\\shell\\wincvs\\command]"
  PATH)

# TortoiseCVS (in case CVSNT was installed in the same directory)

get_filename_component(
  TortoiseCVS_Folder_Command
  "[HKEY_CLASSES_ROOT\\CVS\\shell\\open\\command]"
  PATH)

get_filename_component(
  TortoiseCVS_DefaultIcon
  "[HKEY_CLASSES_ROOT\\CVS\\DefaultIcon]"
  PATH)

find_program(CVS_EXECUTABLE cvs
  ${TortoiseCVS_DefaultIcon}
  ${TortoiseCVS_Folder_Command}
  ${WinCVS_Folder_Command}
  ${CVSNT_Services_EventMessagePath}
  ${CVSNT_TypeLib_Win32}
  "[HKEY_LOCAL_MACHINE\\SOFTWARE\\CVS\\Pserver;InstallPath]"
  DOC "CVS command line client"
  )
mark_as_advanced(CVS_EXECUTABLE)

# Handle the QUIETLY and REQUIRED arguments and set CVS_FOUND to TRUE if
# all listed variables are TRUE

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args(CVS DEFAULT_MSG CVS_EXECUTABLE)
