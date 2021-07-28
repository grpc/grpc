# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindHTMLHelp
------------

This module looks for Microsoft HTML Help Compiler

It defines:

::

   HTML_HELP_COMPILER     : full path to the Compiler (hhc.exe)
   HTML_HELP_INCLUDE_PATH : include path to the API (htmlhelp.h)
   HTML_HELP_LIBRARY      : full path to the library (htmlhelp.lib)
#]=======================================================================]

if(WIN32)

  find_program(HTML_HELP_COMPILER
    NAMES hhc
    PATHS
      "[HKEY_CURRENT_USER\\Software\\Microsoft\\HTML Help Workshop;InstallDir]"
    PATH_SUFFIXES "HTML Help Workshop"
    )

  get_filename_component(HTML_HELP_COMPILER_PATH "${HTML_HELP_COMPILER}" PATH)

  find_path(HTML_HELP_INCLUDE_PATH
    NAMES htmlhelp.h
    PATHS
      "${HTML_HELP_COMPILER_PATH}/include"
      "[HKEY_CURRENT_USER\\Software\\Microsoft\\HTML Help Workshop;InstallDir]/include"
    PATH_SUFFIXES "HTML Help Workshop/include"
    )

  find_library(HTML_HELP_LIBRARY
    NAMES htmlhelp
    PATHS
      "${HTML_HELP_COMPILER_PATH}/lib"
      "[HKEY_CURRENT_USER\\Software\\Microsoft\\HTML Help Workshop;InstallDir]/lib"
    PATH_SUFFIXES "HTML Help Workshop/lib"
    )

  mark_as_advanced(
    HTML_HELP_COMPILER
    HTML_HELP_INCLUDE_PATH
    HTML_HELP_LIBRARY
    )

endif()
