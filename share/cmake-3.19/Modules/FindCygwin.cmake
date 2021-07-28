# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindCygwin
----------

Find Cygwin, a POSIX-compatible environment that runs natively
on Microsoft Windows
#]=======================================================================]

if (WIN32)
  if(CYGWIN_INSTALL_PATH)
    set(CYGWIN_BAT "${CYGWIN_INSTALL_PATH}/cygwin.bat")
  endif()

  find_program(CYGWIN_BAT
    NAMES cygwin.bat
    PATHS
      "C:/Cygwin"
      "C:/Cygwin64"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Cygwin\\setup;rootdir]"
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Cygnus Solutions\\Cygwin\\mounts v2\\/;native]"
  )
  get_filename_component(CYGWIN_INSTALL_PATH "${CYGWIN_BAT}" DIRECTORY)
  mark_as_advanced(CYGWIN_BAT)

endif ()
