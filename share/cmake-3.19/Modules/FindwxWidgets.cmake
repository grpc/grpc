# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindwxWidgets
-------------

Find a wxWidgets (a.k.a., wxWindows) installation.

This module finds if wxWidgets is installed and selects a default
configuration to use.  wxWidgets is a modular library.  To specify the
modules that you will use, you need to name them as components to the
package:

find_package(wxWidgets COMPONENTS core base ... OPTIONAL_COMPONENTS net ...)

There are two search branches: a windows style and a unix style.  For
windows, the following variables are searched for and set to defaults
in case of multiple choices.  Change them if the defaults are not
desired (i.e., these are the only variables you should change to
select a configuration):

::

  wxWidgets_ROOT_DIR      - Base wxWidgets directory
                            (e.g., C:/wxWidgets-2.6.3).
  wxWidgets_LIB_DIR       - Path to wxWidgets libraries
                            (e.g., C:/wxWidgets-2.6.3/lib/vc_lib).
  wxWidgets_CONFIGURATION - Configuration to use
                            (e.g., msw, mswd, mswu, mswunivud, etc.)
  wxWidgets_EXCLUDE_COMMON_LIBRARIES
                          - Set to TRUE to exclude linking of
                            commonly required libs (e.g., png tiff
                            jpeg zlib regex expat).



For unix style it uses the wx-config utility.  You can select between
debug/release, unicode/ansi, universal/non-universal, and
static/shared in the QtDialog or ccmake interfaces by turning ON/OFF
the following variables:

::

  wxWidgets_USE_DEBUG
  wxWidgets_USE_UNICODE
  wxWidgets_USE_UNIVERSAL
  wxWidgets_USE_STATIC



There is also a wxWidgets_CONFIG_OPTIONS variable for all other
options that need to be passed to the wx-config utility.  For example,
to use the base toolkit found in the /usr/local path, set the variable
(before calling the FIND_PACKAGE command) as such:

::

  set(wxWidgets_CONFIG_OPTIONS --toolkit=base --prefix=/usr)



The following are set after the configuration is done for both windows
and unix style:

::

  wxWidgets_FOUND            - Set to TRUE if wxWidgets was found.
  wxWidgets_INCLUDE_DIRS     - Include directories for WIN32
                               i.e., where to find "wx/wx.h" and
                               "wx/setup.h"; possibly empty for unices.
  wxWidgets_LIBRARIES        - Path to the wxWidgets libraries.
  wxWidgets_LIBRARY_DIRS     - compile time link dirs, useful for
                               rpath on UNIX. Typically an empty string
                               in WIN32 environment.
  wxWidgets_DEFINITIONS      - Contains defines required to compile/link
                               against WX, e.g. WXUSINGDLL
  wxWidgets_DEFINITIONS_DEBUG- Contains defines required to compile/link
                               against WX debug builds, e.g. __WXDEBUG__
  wxWidgets_CXX_FLAGS        - Include dirs and compiler flags for
                               unices, empty on WIN32. Essentially
                               "`wx-config --cxxflags`".
  wxWidgets_USE_FILE         - Convenience include file.



Sample usage:

::

   # Note that for MinGW users the order of libs is important!
   find_package(wxWidgets COMPONENTS gl core base OPTIONAL_COMPONENTS net)
   if(wxWidgets_FOUND)
     include(${wxWidgets_USE_FILE})
     # and for each of your dependent executable/library targets:
     target_link_libraries(<YourTarget> ${wxWidgets_LIBRARIES})
   endif()



If wxWidgets is required (i.e., not an optional part):

::

   find_package(wxWidgets REQUIRED gl core base OPTIONAL_COMPONENTS net)
   include(${wxWidgets_USE_FILE})
   # and for each of your dependent executable/library targets:
   target_link_libraries(<YourTarget> ${wxWidgets_LIBRARIES})
#]=======================================================================]

#
# FIXME: check this and provide a correct sample usage...
#        Remember to connect back to the upper text.
# Sample usage with monolithic wx build:
#
#   find_package(wxWidgets COMPONENTS mono)
#   ...

# NOTES
#
# This module has been tested on the WIN32 platform with wxWidgets
# 2.6.2, 2.6.3, and 2.5.3. However, it has been designed to
# easily extend support to all possible builds, e.g., static/shared,
# debug/release, unicode, universal, multilib/monolithic, etc..
#
# If you want to use the module and your build type is not supported
# out-of-the-box, please contact me to exchange information on how
# your system is setup and I'll try to add support for it.
#
# AUTHOR
#
# Miguel A. Figueroa-Villanueva (miguelf at ieee dot org).
# Jan Woetzel (jw at mip.informatik.uni-kiel.de).
#
# Based on previous works of:
# Jan Woetzel (FindwxWindows.cmake),
# Jorgen Bodde and Jerry Fath (FindwxWin.cmake).

# TODO/ideas
#
# (1) Option/Setting to use all available wx libs
# In contrast to expert developer who lists the
# minimal set of required libs in wxWidgets_USE_LIBS
# there is the newbie user:
#   - who just wants to link against WX with more 'magic'
#   - doesn't know the internal structure of WX or how it was built,
#     in particular if it is monolithic or not
#   - want to link against all available WX libs
# Basically, the intent here is to mimic what wx-config would do by
# default (i.e., `wx-config --libs`).
#
# Possible solution:
#   Add a reserved keyword "std" that initializes to what wx-config
# would default to. If the user has not set the wxWidgets_USE_LIBS,
# default to "std" instead of "base core" as it is now. To implement
# "std" will basically boil down to a FOR_EACH lib-FOUND, but maybe
# checking whether a minimal set was found.


# FIXME: This and all the DBG_MSG calls should be removed after the
# module stabilizes.
#
# Helper macro to control the debugging output globally. There are
# two versions for controlling how verbose your output should be.
macro(DBG_MSG _MSG)
#  message(STATUS
#    "${CMAKE_CURRENT_LIST_FILE}(${CMAKE_CURRENT_LIST_LINE}): ${_MSG}")
endmacro()
macro(DBG_MSG_V _MSG)
#  message(STATUS
#    "${CMAKE_CURRENT_LIST_FILE}(${CMAKE_CURRENT_LIST_LINE}): ${_MSG}")
endmacro()

# Clear return values in case the module is loaded more than once.
set(wxWidgets_FOUND FALSE)
set(wxWidgets_INCLUDE_DIRS "")
set(wxWidgets_LIBRARIES    "")
set(wxWidgets_LIBRARY_DIRS "")
set(wxWidgets_CXX_FLAGS    "")

# DEPRECATED: This is a patch to support the DEPRECATED use of
# wxWidgets_USE_LIBS.
#
# If wxWidgets_USE_LIBS is set:
# - if using <components>, then override wxWidgets_USE_LIBS
# - else set wxWidgets_FIND_COMPONENTS to wxWidgets_USE_LIBS
if(wxWidgets_USE_LIBS AND NOT wxWidgets_FIND_COMPONENTS)
  set(wxWidgets_FIND_COMPONENTS ${wxWidgets_USE_LIBS})
endif()
DBG_MSG("wxWidgets_FIND_COMPONENTS : ${wxWidgets_FIND_COMPONENTS}")

# Add the convenience use file if available.
#
# Get dir of this file which may reside in:
# - CMAKE_MAKE_ROOT/Modules on CMake installation
# - CMAKE_MODULE_PATH if user prefers his own specialized version
set(wxWidgets_USE_FILE "")
get_filename_component(
  wxWidgets_CURRENT_LIST_DIR ${CMAKE_CURRENT_LIST_FILE} PATH)
# Prefer an existing customized version, but the user might override
# the FindwxWidgets module and not the UsewxWidgets one.
if(EXISTS "${wxWidgets_CURRENT_LIST_DIR}/UsewxWidgets.cmake")
  set(wxWidgets_USE_FILE
    "${wxWidgets_CURRENT_LIST_DIR}/UsewxWidgets.cmake")
else()
  set(wxWidgets_USE_FILE UsewxWidgets)
endif()

#=====================================================================
# Determine whether unix or win32 paths should be used
#=====================================================================
if(WIN32 AND NOT CYGWIN AND NOT MSYS AND NOT CMAKE_CROSSCOMPILING)
  set(wxWidgets_FIND_STYLE "win32")
else()
  set(wxWidgets_FIND_STYLE "unix")
endif()

#=====================================================================
# WIN32_FIND_STYLE
#=====================================================================
if(wxWidgets_FIND_STYLE STREQUAL "win32")
  # Useful common wx libs needed by almost all components.
  set(wxWidgets_COMMON_LIBRARIES png tiff jpeg zlib regex expat)

  # DEPRECATED: Use find_package(wxWidgets COMPONENTS mono) instead.
  if(NOT wxWidgets_FIND_COMPONENTS)
    if(wxWidgets_USE_MONOLITHIC)
      set(wxWidgets_FIND_COMPONENTS mono)
    else()
      set(wxWidgets_FIND_COMPONENTS core base) # this is default
    endif()
  endif()

  # Add the common (usually required libs) unless
  # wxWidgets_EXCLUDE_COMMON_LIBRARIES has been set.
  if(NOT wxWidgets_EXCLUDE_COMMON_LIBRARIES)
    list(APPEND wxWidgets_FIND_COMPONENTS
      ${wxWidgets_COMMON_LIBRARIES})
  endif()

  #-------------------------------------------------------------------
  # WIN32: Helper MACROS
  #-------------------------------------------------------------------
  #
  # Get filename components for a configuration. For example,
  #   if _CONFIGURATION = mswunivud, then _PF="msw", _UNV=univ, _UCD=u _DBG=d
  #   if _CONFIGURATION = mswu,      then _PF="msw", _UNV="",   _UCD=u _DBG=""
  #
  macro(WX_GET_NAME_COMPONENTS _CONFIGURATION _PF _UNV _UCD _DBG)
    DBG_MSG_V(${_CONFIGURATION})
    string(REGEX MATCH "univ" ${_UNV} "${_CONFIGURATION}")
    string(REGEX REPLACE "[msw|qt].*(u)[d]*$" "u" ${_UCD} "${_CONFIGURATION}")
    if(${_UCD} STREQUAL ${_CONFIGURATION})
      set(${_UCD} "")
    endif()
    string(REGEX MATCH "d$" ${_DBG} "${_CONFIGURATION}")
    string(REGEX MATCH "^[msw|qt]*" ${_PF} "${_CONFIGURATION}")
  endmacro()

  #
  # Find libraries associated to a configuration.
  #
  macro(WX_FIND_LIBS _PF _UNV _UCD _DBG)
    DBG_MSG_V("m_unv = ${_UNV}")
    DBG_MSG_V("m_ucd = ${_UCD}")
    DBG_MSG_V("m_dbg = ${_DBG}")

    # FIXME: What if both regex libs are available. regex should be
    # found outside the loop and only wx${LIB}${_UCD}${_DBG}.
    # Find wxWidgets common libraries.
    foreach(LIB ${wxWidgets_COMMON_LIBRARIES} scintilla)
      find_library(WX_${LIB}${_DBG}
        NAMES
        wx${LIB}${_UCD}${_DBG} # for regex
        wx${LIB}${_DBG}
        PATHS ${WX_LIB_DIR}
        NO_DEFAULT_PATH
        )
      mark_as_advanced(WX_${LIB}${_DBG})
    endforeach()

    # Find wxWidgets multilib base libraries.
    find_library(WX_base${_DBG}
      NAMES
      wxbase31${_UCD}${_DBG}
      wxbase30${_UCD}${_DBG}
      wxbase29${_UCD}${_DBG}
      wxbase28${_UCD}${_DBG}
      wxbase27${_UCD}${_DBG}
      wxbase26${_UCD}${_DBG}
      wxbase25${_UCD}${_DBG}
      PATHS ${WX_LIB_DIR}
      NO_DEFAULT_PATH
      )
    mark_as_advanced(WX_base${_DBG})
    foreach(LIB net odbc xml)
      find_library(WX_${LIB}${_DBG}
        NAMES
        wxbase31${_UCD}${_DBG}_${LIB}
        wxbase30${_UCD}${_DBG}_${LIB}
        wxbase29${_UCD}${_DBG}_${LIB}
        wxbase28${_UCD}${_DBG}_${LIB}
        wxbase27${_UCD}${_DBG}_${LIB}
        wxbase26${_UCD}${_DBG}_${LIB}
        wxbase25${_UCD}${_DBG}_${LIB}
        PATHS ${WX_LIB_DIR}
        NO_DEFAULT_PATH
        )
      mark_as_advanced(WX_${LIB}${_DBG})
    endforeach()

    # Find wxWidgets monolithic library.
    find_library(WX_mono${_DBG}
      NAMES
      wx${_PF}${_UNV}31${_UCD}${_DBG}
      wx${_PF}${_UNV}30${_UCD}${_DBG}
      wx${_PF}${_UNV}29${_UCD}${_DBG}
      wx${_PF}${_UNV}28${_UCD}${_DBG}
      wx${_PF}${_UNV}27${_UCD}${_DBG}
      wx${_PF}${_UNV}26${_UCD}${_DBG}
      wx${_PF}${_UNV}25${_UCD}${_DBG}
      PATHS ${WX_LIB_DIR}
      NO_DEFAULT_PATH
      )
    mark_as_advanced(WX_mono${_DBG})

    # Find wxWidgets multilib libraries.
    foreach(LIB core adv aui html media xrc dbgrid gl qa richtext
                stc ribbon propgrid webview)
      find_library(WX_${LIB}${_DBG}
        NAMES
        wx${_PF}${_UNV}31${_UCD}${_DBG}_${LIB}
        wx${_PF}${_UNV}30${_UCD}${_DBG}_${LIB}
        wx${_PF}${_UNV}29${_UCD}${_DBG}_${LIB}
        wx${_PF}${_UNV}28${_UCD}${_DBG}_${LIB}
        wx${_PF}${_UNV}27${_UCD}${_DBG}_${LIB}
        wx${_PF}${_UNV}26${_UCD}${_DBG}_${LIB}
        wx${_PF}${_UNV}25${_UCD}${_DBG}_${LIB}
        PATHS ${WX_LIB_DIR}
        NO_DEFAULT_PATH
        )
      mark_as_advanced(WX_${LIB}${_DBG})
    endforeach()
  endmacro()

  #
  # Clear all library paths, so that FIND_LIBRARY refinds them.
  #
  # Clear a lib, reset its found flag, and mark as advanced.
  macro(WX_CLEAR_LIB _LIB)
    set(${_LIB} "${_LIB}-NOTFOUND" CACHE FILEPATH "Cleared." FORCE)
    set(${_LIB}_FOUND FALSE)
    mark_as_advanced(${_LIB})
  endmacro()
  # Clear all debug or release library paths (arguments are "d" or "").
  macro(WX_CLEAR_ALL_LIBS _DBG)
    # Clear wxWidgets common libraries.
    foreach(LIB ${wxWidgets_COMMON_LIBRARIES} scintilla)
      WX_CLEAR_LIB(WX_${LIB}${_DBG})
    endforeach()

    # Clear wxWidgets multilib base libraries.
    WX_CLEAR_LIB(WX_base${_DBG})
    foreach(LIB net odbc xml)
      WX_CLEAR_LIB(WX_${LIB}${_DBG})
    endforeach()

    # Clear wxWidgets monolithic library.
    WX_CLEAR_LIB(WX_mono${_DBG})

    # Clear wxWidgets multilib libraries.
    foreach(LIB core adv aui html media xrc dbgrid gl qa richtext
                webview stc ribbon propgrid)
      WX_CLEAR_LIB(WX_${LIB}${_DBG})
    endforeach()
  endmacro()
  # Clear all wxWidgets debug libraries.
  macro(WX_CLEAR_ALL_DBG_LIBS)
    WX_CLEAR_ALL_LIBS("d")
  endmacro()
  # Clear all wxWidgets release libraries.
  macro(WX_CLEAR_ALL_REL_LIBS)
    WX_CLEAR_ALL_LIBS("")
  endmacro()

  #
  # Set the wxWidgets_LIBRARIES variable.
  # Also, Sets output variable wxWidgets_FOUND to FALSE if it fails.
  #
  macro(WX_SET_LIBRARIES _LIBS _DBG)
    DBG_MSG_V("Looking for ${${_LIBS}}")
    if(WX_USE_REL_AND_DBG)
      foreach(LIB ${${_LIBS}})
        DBG_MSG_V("Searching for ${LIB} and ${LIB}d")
        DBG_MSG_V("WX_${LIB}  : ${WX_${LIB}}")
        DBG_MSG_V("WX_${LIB}d : ${WX_${LIB}d}")
        if(WX_${LIB} AND WX_${LIB}d)
          DBG_MSG_V("Found ${LIB} and ${LIB}d")
          list(APPEND wxWidgets_LIBRARIES
            debug ${WX_${LIB}d} optimized ${WX_${LIB}}
            )
          set(wxWidgets_${LIB}_FOUND TRUE)
        elseif(NOT wxWidgets_FIND_REQUIRED_${LIB})
          DBG_MSG_V("- ignored optional missing WX_${LIB}=${WX_${LIB}} or WX_${LIB}d=${WX_${LIB}d}")
        else()
          DBG_MSG_V("- not found due to missing WX_${LIB}=${WX_${LIB}} or WX_${LIB}d=${WX_${LIB}d}")
          set(wxWidgets_FOUND FALSE)
        endif()
      endforeach()
    else()
      foreach(LIB ${${_LIBS}})
        DBG_MSG_V("Searching for ${LIB}${_DBG}")
        DBG_MSG_V("WX_${LIB}${_DBG} : ${WX_${LIB}${_DBG}}")
        if(WX_${LIB}${_DBG})
          DBG_MSG_V("Found ${LIB}${_DBG}")
          list(APPEND wxWidgets_LIBRARIES ${WX_${LIB}${_DBG}})
          set(wxWidgets_${LIB}_FOUND TRUE)
        elseif(NOT wxWidgets_FIND_REQUIRED_${LIB})
          DBG_MSG_V("- ignored optional missing WX_${LIB}${_DBG}=${WX_${LIB}${_DBG}}")
        else()
          DBG_MSG_V("- not found due to missing WX_${LIB}${_DBG}=${WX_${LIB}${_DBG}}")
          set(wxWidgets_FOUND FALSE)
        endif()
      endforeach()
    endif()

    DBG_MSG_V("OpenGL")
    list(FIND ${_LIBS} gl WX_USE_GL)
    if(NOT WX_USE_GL EQUAL -1)
      DBG_MSG_V("- is required.")
      list(APPEND wxWidgets_LIBRARIES opengl32 glu32)
    endif()

    list(APPEND wxWidgets_LIBRARIES winmm comctl32 uuid oleacc uxtheme rpcrt4 shlwapi version wsock32)
  endmacro()

  #-------------------------------------------------------------------
  # WIN32: Start actual work.
  #-------------------------------------------------------------------

  # Look for an installation tree.
  find_path(wxWidgets_ROOT_DIR
    NAMES include/wx/wx.h
    PATHS
      ENV wxWidgets_ROOT_DIR
      ENV WXWIN
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\wxWidgets_is1;Inno Setup: App Path]"  # WX 2.6.x
      C:/
      D:/
      ENV ProgramFiles
    PATH_SUFFIXES
      wxWidgets-3.1.0
      wxWidgets-3.0.2
      wxWidgets-3.0.1
      wxWidgets-3.0.0
      wxWidgets-2.9.5
      wxWidgets-2.9.4
      wxWidgets-2.9.3
      wxWidgets-2.9.2
      wxWidgets-2.9.1
      wxWidgets-2.9.0
      wxWidgets-2.8.9
      wxWidgets-2.8.8
      wxWidgets-2.8.7
      wxWidgets-2.8.6
      wxWidgets-2.8.5
      wxWidgets-2.8.4
      wxWidgets-2.8.3
      wxWidgets-2.8.2
      wxWidgets-2.8.1
      wxWidgets-2.8.0
      wxWidgets-2.7.4
      wxWidgets-2.7.3
      wxWidgets-2.7.2
      wxWidgets-2.7.1
      wxWidgets-2.7.0
      wxWidgets-2.7.0-1
      wxWidgets-2.6.4
      wxWidgets-2.6.3
      wxWidgets-2.6.2
      wxWidgets-2.6.1
      wxWidgets-2.5.4
      wxWidgets-2.5.3
      wxWidgets-2.5.2
      wxWidgets-2.5.1
      wxWidgets
    DOC "wxWidgets base/installation directory"
    )

  # If wxWidgets_ROOT_DIR changed, clear lib dir.
  if(NOT WX_ROOT_DIR STREQUAL wxWidgets_ROOT_DIR)
    set(WX_ROOT_DIR ${wxWidgets_ROOT_DIR}
        CACHE INTERNAL "wxWidgets_ROOT_DIR")
    set(wxWidgets_LIB_DIR "wxWidgets_LIB_DIR-NOTFOUND"
        CACHE PATH "Cleared." FORCE)
  endif()

  if(WX_ROOT_DIR)
    # Select one default tree inside the already determined wx tree.
    # Prefer static/shared order usually consistent with build
    # settings.
    set(_WX_TOOL "")
    set(_WX_TOOLVER "")
    set(_WX_ARCH "")
    if(MINGW)
      set(_WX_TOOL gcc)
    elseif(MSVC)
      set(_WX_TOOL vc)
      set(_WX_TOOLVER ${MSVC_TOOLSET_VERSION})
      # support for a lib/vc14x_x64_dll/ path from wxW 3.1.3 distribution
      string(REGEX REPLACE ".$" "x" _WX_TOOLVERx ${_WX_TOOLVER})
      if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_WX_ARCH _x64)
      endif()
    endif()
    if(BUILD_SHARED_LIBS)
      find_path(wxWidgets_LIB_DIR
        NAMES
          qtu/wx/setup.h
          qtud/wx/setup.h
          msw/wx/setup.h
          mswd/wx/setup.h
          mswu/wx/setup.h
          mswud/wx/setup.h
          mswuniv/wx/setup.h
          mswunivd/wx/setup.h
          mswunivu/wx/setup.h
          mswunivud/wx/setup.h
        PATHS
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVER}_xp${_WX_ARCH}_dll   # prefer shared
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVER}${_WX_ARCH}_dll   # prefer shared
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVERx}_xp${_WX_ARCH}_dll   # prefer shared
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVERx}${_WX_ARCH}_dll   # prefer shared
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_ARCH}_dll                 # prefer shared
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVER}_xp${_WX_ARCH}_lib
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVER}${_WX_ARCH}_lib
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVERx}_xp${_WX_ARCH}_lib
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVERx}${_WX_ARCH}_lib
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_ARCH}_lib
        DOC "Path to wxWidgets libraries"
        NO_DEFAULT_PATH
        )
    else()
      find_path(wxWidgets_LIB_DIR
        NAMES
          qtu/wx/setup.h
          qtud/wx/setup.h
          msw/wx/setup.h
          mswd/wx/setup.h
          mswu/wx/setup.h
          mswud/wx/setup.h
          mswuniv/wx/setup.h
          mswunivd/wx/setup.h
          mswunivu/wx/setup.h
          mswunivud/wx/setup.h
        PATHS
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVER}_xp${_WX_ARCH}_lib   # prefer static
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVER}${_WX_ARCH}_lib   # prefer static
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVERx}_xp${_WX_ARCH}_lib   # prefer static
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVERx}${_WX_ARCH}_lib   # prefer static
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_ARCH}_lib                 # prefer static
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVER}_xp${_WX_ARCH}_dll
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVER}${_WX_ARCH}_dll
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVERx}_xp${_WX_ARCH}_dll
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_TOOLVERx}${_WX_ARCH}_dll
        ${WX_ROOT_DIR}/lib/${_WX_TOOL}${_WX_ARCH}_dll
        DOC "Path to wxWidgets libraries"
        NO_DEFAULT_PATH
        )
    endif()
    unset(_WX_TOOL)
    unset(_WX_TOOLVER)
    unset(_WX_ARCH)

    # If wxWidgets_LIB_DIR changed, clear all libraries.
    if(NOT WX_LIB_DIR STREQUAL wxWidgets_LIB_DIR)
      set(WX_LIB_DIR ${wxWidgets_LIB_DIR} CACHE INTERNAL "wxWidgets_LIB_DIR")
      WX_CLEAR_ALL_DBG_LIBS()
      WX_CLEAR_ALL_REL_LIBS()
    endif()

    if(WX_LIB_DIR)
      # If building shared libs, define WXUSINGDLL to use dllimport.
      if(WX_LIB_DIR MATCHES "[dD][lL][lL]")
        set(wxWidgets_DEFINITIONS WXUSINGDLL)
        DBG_MSG_V("detected SHARED/DLL tree WX_LIB_DIR=${WX_LIB_DIR}")
      endif()

      # Search for available configuration types.
      foreach(CFG mswunivud mswunivd mswud mswd mswunivu mswuniv mswu msw qt qtd qtu qtud)
        set(WX_${CFG}_FOUND FALSE)
        if(EXISTS ${WX_LIB_DIR}/${CFG})
          list(APPEND WX_CONFIGURATION_LIST ${CFG})
          set(WX_${CFG}_FOUND TRUE)
          set(WX_CONFIGURATION ${CFG})
        endif()
      endforeach()
      DBG_MSG_V("WX_CONFIGURATION_LIST=${WX_CONFIGURATION_LIST}")

      if(WX_CONFIGURATION)
        set(wxWidgets_FOUND TRUE)

        # If the selected configuration wasn't found force the default
        # one. Otherwise, use it but still force a refresh for
        # updating the doc string with the current list of available
        # configurations.
        if(NOT WX_${wxWidgets_CONFIGURATION}_FOUND)
          set(wxWidgets_CONFIGURATION ${WX_CONFIGURATION} CACHE STRING
            "Set wxWidgets configuration (${WX_CONFIGURATION_LIST})" FORCE)
        else()
          set(wxWidgets_CONFIGURATION ${wxWidgets_CONFIGURATION} CACHE STRING
            "Set wxWidgets configuration (${WX_CONFIGURATION_LIST})" FORCE)
        endif()

        # If release config selected, and both release/debug exist.
        if(WX_${wxWidgets_CONFIGURATION}d_FOUND)
          option(wxWidgets_USE_REL_AND_DBG
            "Use release and debug configurations?" TRUE)
          set(WX_USE_REL_AND_DBG ${wxWidgets_USE_REL_AND_DBG})
        else()
          # If the option exists (already in cache), force it false.
          if(wxWidgets_USE_REL_AND_DBG)
            set(wxWidgets_USE_REL_AND_DBG FALSE CACHE BOOL
              "No ${wxWidgets_CONFIGURATION}d found." FORCE)
          endif()
          set(WX_USE_REL_AND_DBG FALSE)
        endif()

        # Get configuration parameters from the name.
        WX_GET_NAME_COMPONENTS(${wxWidgets_CONFIGURATION} PF UNV UCD DBG)

        # Set wxWidgets lib setup include directory.
        if(EXISTS ${WX_LIB_DIR}/${wxWidgets_CONFIGURATION}/wx/setup.h)
          set(wxWidgets_INCLUDE_DIRS
            ${WX_LIB_DIR}/${wxWidgets_CONFIGURATION})
        else()
          DBG_MSG("wxWidgets_FOUND FALSE because ${WX_LIB_DIR}/${wxWidgets_CONFIGURATION}/wx/setup.h does not exists.")
          set(wxWidgets_FOUND FALSE)
        endif()

        # Set wxWidgets main include directory.
        if(EXISTS ${WX_ROOT_DIR}/include/wx/wx.h)
          list(APPEND wxWidgets_INCLUDE_DIRS ${WX_ROOT_DIR}/include)
        else()
          DBG_MSG("wxWidgets_FOUND FALSE because WX_ROOT_DIR=${WX_ROOT_DIR} has no ${WX_ROOT_DIR}/include/wx/wx.h")
          set(wxWidgets_FOUND FALSE)
        endif()

        # Find wxWidgets libraries.
        WX_FIND_LIBS("${PF}" "${UNV}" "${UCD}" "${DBG}")
        if(WX_USE_REL_AND_DBG)
          WX_FIND_LIBS("${PF}" "${UNV}" "${UCD}" "d")
        endif()

        # Settings for requested libs (i.e., include dir, libraries, etc.).
        WX_SET_LIBRARIES(wxWidgets_FIND_COMPONENTS "${DBG}")

        # Add necessary definitions for unicode builds
        if("${UCD}" STREQUAL "u")
          list(APPEND wxWidgets_DEFINITIONS UNICODE _UNICODE)
        endif()

        # Add necessary definitions for debug builds
        set(wxWidgets_DEFINITIONS_DEBUG _DEBUG __WXDEBUG__)

      endif()
    endif()
  endif()

#=====================================================================
# UNIX_FIND_STYLE
#=====================================================================
else()
  if(wxWidgets_FIND_STYLE STREQUAL "unix")
    #-----------------------------------------------------------------
    # UNIX: Helper MACROS
    #-----------------------------------------------------------------
    #
    # Set the default values based on "wx-config --selected-config".
    #
    macro(WX_CONFIG_SELECT_GET_DEFAULT)
      execute_process(
        COMMAND sh "${wxWidgets_CONFIG_EXECUTABLE}"
          ${wxWidgets_CONFIG_OPTIONS} --selected-config
        OUTPUT_VARIABLE _wx_selected_config
        RESULT_VARIABLE _wx_result
        ERROR_QUIET
        )
      if(_wx_result EQUAL 0)
        foreach(_opt_name debug static unicode universal)
          string(TOUPPER ${_opt_name} _upper_opt_name)
          if(_wx_selected_config MATCHES "${_opt_name}")
            set(wxWidgets_DEFAULT_${_upper_opt_name} ON)
          else()
            set(wxWidgets_DEFAULT_${_upper_opt_name} OFF)
          endif()
        endforeach()
      else()
        foreach(_upper_opt_name DEBUG STATIC UNICODE UNIVERSAL)
          set(wxWidgets_DEFAULT_${_upper_opt_name} OFF)
        endforeach()
      endif()
    endmacro()

    #
    # Query a boolean configuration option to determine if the system
    # has both builds available. If so, provide the selection option
    # to the user.
    #
    macro(WX_CONFIG_SELECT_QUERY_BOOL _OPT_NAME _OPT_HELP)
      execute_process(
        COMMAND sh "${wxWidgets_CONFIG_EXECUTABLE}"
          ${wxWidgets_CONFIG_OPTIONS} --${_OPT_NAME}=yes
        RESULT_VARIABLE _wx_result_yes
        OUTPUT_QUIET
        ERROR_QUIET
        )
      execute_process(
        COMMAND sh "${wxWidgets_CONFIG_EXECUTABLE}"
          ${wxWidgets_CONFIG_OPTIONS} --${_OPT_NAME}=no
        RESULT_VARIABLE _wx_result_no
        OUTPUT_QUIET
        ERROR_QUIET
        )
      string(TOUPPER ${_OPT_NAME} _UPPER_OPT_NAME)
      if(_wx_result_yes EQUAL 0 AND _wx_result_no EQUAL 0)
        option(wxWidgets_USE_${_UPPER_OPT_NAME}
          ${_OPT_HELP} ${wxWidgets_DEFAULT_${_UPPER_OPT_NAME}})
      else()
        # If option exists (already in cache), force to available one.
        if(DEFINED wxWidgets_USE_${_UPPER_OPT_NAME})
          if(_wx_result_yes EQUAL 0)
            set(wxWidgets_USE_${_UPPER_OPT_NAME} ON  CACHE BOOL ${_OPT_HELP} FORCE)
          else()
            set(wxWidgets_USE_${_UPPER_OPT_NAME} OFF CACHE BOOL ${_OPT_HELP} FORCE)
          endif()
        endif()
      endif()
    endmacro()

    #
    # Set wxWidgets_SELECT_OPTIONS to wx-config options for selecting
    # among multiple builds.
    #
    macro(WX_CONFIG_SELECT_SET_OPTIONS)
      set(wxWidgets_SELECT_OPTIONS ${wxWidgets_CONFIG_OPTIONS})
      foreach(_opt_name debug static unicode universal)
        string(TOUPPER ${_opt_name} _upper_opt_name)
        if(DEFINED wxWidgets_USE_${_upper_opt_name})
          if(wxWidgets_USE_${_upper_opt_name})
            list(APPEND wxWidgets_SELECT_OPTIONS --${_opt_name}=yes)
          else()
            list(APPEND wxWidgets_SELECT_OPTIONS --${_opt_name}=no)
          endif()
        endif()
      endforeach()
    endmacro()

    #-----------------------------------------------------------------
    # UNIX: Start actual work.
    #-----------------------------------------------------------------
    # Support cross-compiling, only search in the target platform.
    #
    # Look for wx-config -- this can be set in the environment,
    # or try versioned and toolchain-versioned variants of the -config
    # executable as well.
    find_program(wxWidgets_CONFIG_EXECUTABLE
      NAMES
        $ENV{WX_CONFIG}
        wx-config
        wx-config-3.1 wx-config-3.0 wx-config-2.9 wx-config-2.8
        wxgtk3u-3.1-config wxgtk3u-3.0-config wxgtk2u-2.8-config
      DOC "Location of wxWidgets library configuration provider binary (wx-config)."
      ONLY_CMAKE_FIND_ROOT_PATH
      )

    if(wxWidgets_CONFIG_EXECUTABLE)
      set(wxWidgets_FOUND TRUE)

      # get defaults based on "wx-config --selected-config"
      WX_CONFIG_SELECT_GET_DEFAULT()

      # for each option: if both builds are available, provide option
      WX_CONFIG_SELECT_QUERY_BOOL(debug "Use debug build?")
      WX_CONFIG_SELECT_QUERY_BOOL(unicode "Use unicode build?")
      WX_CONFIG_SELECT_QUERY_BOOL(universal "Use universal build?")
      WX_CONFIG_SELECT_QUERY_BOOL(static "Link libraries statically?")

      # process selection to set wxWidgets_SELECT_OPTIONS
      WX_CONFIG_SELECT_SET_OPTIONS()
      DBG_MSG("wxWidgets_SELECT_OPTIONS=${wxWidgets_SELECT_OPTIONS}")

      # run the wx-config program to get cxxflags
      execute_process(
        COMMAND sh "${wxWidgets_CONFIG_EXECUTABLE}"
          ${wxWidgets_SELECT_OPTIONS} --cxxflags
        OUTPUT_VARIABLE wxWidgets_CXX_FLAGS
        RESULT_VARIABLE RET
        ERROR_QUIET
        )
      if(RET EQUAL 0)
        string(STRIP "${wxWidgets_CXX_FLAGS}" wxWidgets_CXX_FLAGS)
        separate_arguments(wxWidgets_CXX_FLAGS_LIST NATIVE_COMMAND "${wxWidgets_CXX_FLAGS}")

        DBG_MSG_V("wxWidgets_CXX_FLAGS=${wxWidgets_CXX_FLAGS}")

        # parse definitions and include dirs from cxxflags
        #   drop the -D and -I prefixes
        set(wxWidgets_CXX_FLAGS)
        foreach(arg IN LISTS wxWidgets_CXX_FLAGS_LIST)
          if("${arg}" MATCHES "^-I(.*)$")
            # include directory
            list(APPEND wxWidgets_INCLUDE_DIRS "${CMAKE_MATCH_1}")
          elseif("${arg}" MATCHES "^-D(.*)$")
            # compile definition
            list(APPEND wxWidgets_DEFINITIONS "${CMAKE_MATCH_1}")
          else()
            list(APPEND wxWidgets_CXX_FLAGS "${arg}")
          endif()
        endforeach()

        DBG_MSG_V("wxWidgets_DEFINITIONS=${wxWidgets_DEFINITIONS}")
        DBG_MSG_V("wxWidgets_INCLUDE_DIRS=${wxWidgets_INCLUDE_DIRS}")
        DBG_MSG_V("wxWidgets_CXX_FLAGS=${wxWidgets_CXX_FLAGS}")

      else()
        set(wxWidgets_FOUND FALSE)
        DBG_MSG_V(
          "${wxWidgets_CONFIG_EXECUTABLE} --cxxflags FAILED with RET=${RET}")
      endif()

      # run the wx-config program to get the libs
      # - NOTE: wx-config doesn't verify that the libs requested exist
      #         it just produces the names. Maybe a TRY_COMPILE would
      #         be useful here...
      unset(_cmp_req)
      unset(_cmp_opt)
      foreach(_cmp IN LISTS wxWidgets_FIND_COMPONENTS)
        if(wxWidgets_FIND_REQUIRED_${_cmp})
          list(APPEND _cmp_req "${_cmp}")
        else()
          list(APPEND _cmp_opt "${_cmp}")
        endif()
      endforeach()
      DBG_MSG_V("wxWidgets required components : ${_cmp_req}")
      DBG_MSG_V("wxWidgets optional components : ${_cmp_opt}")
      if(DEFINED _cmp_opt)
        string(REPLACE ";" "," _cmp_opt "--optional-libs ${_cmp_opt}")
      endif()
      string(REPLACE ";" "," _cmp_req "${_cmp_req}")
      execute_process(
        COMMAND sh "${wxWidgets_CONFIG_EXECUTABLE}"
          ${wxWidgets_SELECT_OPTIONS} --libs ${_cmp_req} ${_cmp_opt}
        OUTPUT_VARIABLE wxWidgets_LIBRARIES
        RESULT_VARIABLE RET
        ERROR_QUIET
        )
      if(RET EQUAL 0)
        string(STRIP "${wxWidgets_LIBRARIES}" wxWidgets_LIBRARIES)
        separate_arguments(wxWidgets_LIBRARIES)
        string(REPLACE "-framework;" "-framework "
          wxWidgets_LIBRARIES "${wxWidgets_LIBRARIES}")
        string(REPLACE "-weak_framework;" "-weak_framework "
          wxWidgets_LIBRARIES "${wxWidgets_LIBRARIES}")
        string(REPLACE "-arch;" "-arch "
          wxWidgets_LIBRARIES "${wxWidgets_LIBRARIES}")
        string(REPLACE "-isysroot;" "-isysroot "
          wxWidgets_LIBRARIES "${wxWidgets_LIBRARIES}")

        # extract linkdirs (-L) for rpath (i.e., LINK_DIRECTORIES)
        string(REGEX MATCHALL "-L[^;]+"
          wxWidgets_LIBRARY_DIRS "${wxWidgets_LIBRARIES}")
        string(REGEX REPLACE "-L([^;]+)" "\\1"
          wxWidgets_LIBRARY_DIRS "${wxWidgets_LIBRARY_DIRS}")

        DBG_MSG_V("wxWidgets_LIBRARIES=${wxWidgets_LIBRARIES}")
        DBG_MSG_V("wxWidgets_LIBRARY_DIRS=${wxWidgets_LIBRARY_DIRS}")

      else()
        set(wxWidgets_FOUND FALSE)
        DBG_MSG("${wxWidgets_CONFIG_EXECUTABLE} --libs ${_cmp_req} ${_cmp_opt} FAILED with RET=${RET}")
      endif()
      unset(_cmp_req)
      unset(_cmp_opt)
    endif()

    # When using wx-config in MSYS, the include paths are UNIX style paths which may or may
    # not work correctly depending on you MSYS/MinGW configuration.  CMake expects native
    # paths internally.
    if(wxWidgets_FOUND AND MSYS)
      find_program(_cygpath_exe cygpath ONLY_CMAKE_FIND_ROOT_PATH)
      DBG_MSG_V("_cygpath_exe:  ${_cygpath_exe}")
      if(_cygpath_exe)
        set(_tmp_path "")
        foreach(_path ${wxWidgets_INCLUDE_DIRS})
          execute_process(
            COMMAND cygpath -w ${_path}
            OUTPUT_VARIABLE _native_path
            RESULT_VARIABLE _retv
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            )
          if(_retv EQUAL 0)
            file(TO_CMAKE_PATH ${_native_path} _native_path)
            DBG_MSG_V("Path ${_path} converted to ${_native_path}")
            string(APPEND _tmp_path " ${_native_path}")
          endif()
        endforeach()
        DBG_MSG("Setting wxWidgets_INCLUDE_DIRS = ${_tmp_path}")
        set(wxWidgets_INCLUDE_DIRS ${_tmp_path})
        separate_arguments(wxWidgets_INCLUDE_DIRS)
        list(REMOVE_ITEM wxWidgets_INCLUDE_DIRS "")

        set(_tmp_path "")
        foreach(_path ${wxWidgets_LIBRARY_DIRS})
          execute_process(
            COMMAND cygpath -w ${_path}
            OUTPUT_VARIABLE _native_path
            RESULT_VARIABLE _retv
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            )
          if(_retv EQUAL 0)
            file(TO_CMAKE_PATH ${_native_path} _native_path)
            DBG_MSG_V("Path ${_path} converted to ${_native_path}")
            string(APPEND _tmp_path " ${_native_path}")
          endif()
        endforeach()
        DBG_MSG("Setting wxWidgets_LIBRARY_DIRS = ${_tmp_path}")
        set(wxWidgets_LIBRARY_DIRS ${_tmp_path})
        separate_arguments(wxWidgets_LIBRARY_DIRS)
        list(REMOVE_ITEM wxWidgets_LIBRARY_DIRS "")
      endif()
      unset(_cygpath_exe CACHE)
    endif()

#=====================================================================
# Neither UNIX_FIND_STYLE, nor WIN32_FIND_STYLE
#=====================================================================
  else()
    if(NOT wxWidgets_FIND_QUIETLY)
      message(STATUS
        "${CMAKE_CURRENT_LIST_FILE}(${CMAKE_CURRENT_LIST_LINE}): \n"
        "  Platform unknown/unsupported. It's neither WIN32 nor UNIX "
        "find style."
        )
    endif()
  endif()
endif()

# Check that all libraries are present, as wx-config does not check it
set(_wx_lib_missing "")
foreach(_wx_lib_ ${wxWidgets_LIBRARIES})
  if("${_wx_lib_}" MATCHES "^-l(.*)")
    set(_wx_lib_name "${CMAKE_MATCH_1}")
    unset(_wx_lib_found CACHE)
    find_library(_wx_lib_found NAMES ${_wx_lib_name} HINTS ${wxWidgets_LIBRARY_DIRS})
    if(_wx_lib_found STREQUAL _wx_lib_found-NOTFOUND)
      list(APPEND _wx_lib_missing ${_wx_lib_name})
    endif()
    unset(_wx_lib_found CACHE)
  endif()
endforeach()

if (_wx_lib_missing)
  string(REPLACE ";" " " _wx_lib_missing "${_wx_lib_missing}")
  DBG_MSG_V("wxWidgets not found due to following missing libraries: ${_wx_lib_missing}")
  set(wxWidgets_FOUND FALSE)
  unset(wxWidgets_LIBRARIES)
endif()
unset(_wx_lib_missing)

# Check if a specific version was requested by find_package().
if(wxWidgets_FOUND)
  unset(_wx_filename)
  find_file(_wx_filename wx/version.h PATHS ${wxWidgets_INCLUDE_DIRS} NO_DEFAULT_PATH)
  dbg_msg("_wx_filename:  ${_wx_filename}")

  if(NOT _wx_filename)
    message(FATAL_ERROR "wxWidgets wx/version.h file not found in ${wxWidgets_INCLUDE_DIRS}.")
  endif()

  file(READ "${_wx_filename}" _wx_version_h)
  unset(_wx_filename CACHE)

  string(REGEX REPLACE "^(.*\n)?#define +wxMAJOR_VERSION +([0-9]+).*"
    "\\2" wxWidgets_VERSION_MAJOR "${_wx_version_h}" )
  string(REGEX REPLACE "^(.*\n)?#define +wxMINOR_VERSION +([0-9]+).*"
    "\\2" wxWidgets_VERSION_MINOR "${_wx_version_h}" )
  string(REGEX REPLACE "^(.*\n)?#define +wxRELEASE_NUMBER +([0-9]+).*"
    "\\2" wxWidgets_VERSION_PATCH "${_wx_version_h}" )
  set(wxWidgets_VERSION_STRING
    "${wxWidgets_VERSION_MAJOR}.${wxWidgets_VERSION_MINOR}.${wxWidgets_VERSION_PATCH}" )
  dbg_msg("wxWidgets_VERSION_STRING:    ${wxWidgets_VERSION_STRING}")
endif()

# Debug output:
DBG_MSG("wxWidgets_FOUND           : ${wxWidgets_FOUND}")
DBG_MSG("wxWidgets_INCLUDE_DIRS    : ${wxWidgets_INCLUDE_DIRS}")
DBG_MSG("wxWidgets_LIBRARY_DIRS    : ${wxWidgets_LIBRARY_DIRS}")
DBG_MSG("wxWidgets_LIBRARIES       : ${wxWidgets_LIBRARIES}")
DBG_MSG("wxWidgets_CXX_FLAGS       : ${wxWidgets_CXX_FLAGS}")
DBG_MSG("wxWidgets_USE_FILE        : ${wxWidgets_USE_FILE}")

#=====================================================================
#=====================================================================

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)

# FIXME: set wxWidgets_<comp>_FOUND for wx-config branch
#        and use HANDLE_COMPONENTS on Unix too
if(wxWidgets_FIND_STYLE STREQUAL "win32")
  set(wxWidgets_HANDLE_COMPONENTS "HANDLE_COMPONENTS")
endif()

find_package_handle_standard_args(wxWidgets
  REQUIRED_VARS wxWidgets_LIBRARIES wxWidgets_INCLUDE_DIRS
  VERSION_VAR   wxWidgets_VERSION_STRING
  ${wxWidgets_HANDLE_COMPONENTS}
  )
unset(wxWidgets_HANDLE_COMPONENTS)

#=====================================================================
# Macros for use in wxWidgets apps.
# - This module will not fail to find wxWidgets based on the code
#   below. Hence, it's required to check for validity of:
#
# wxWidgets_wxrc_EXECUTABLE
#=====================================================================

# Resource file compiler.
find_program(wxWidgets_wxrc_EXECUTABLE
  NAMES $ENV{WXRC_CMD} wxrc
  PATHS ${wxWidgets_ROOT_DIR}/utils/wxrc/vc_msw
  DOC "Location of wxWidgets resource file compiler binary (wxrc)"
  )

#
# WX_SPLIT_ARGUMENTS_ON(<keyword> <left> <right> <arg1> <arg2> ...)
#
# Sets <left> and <right> to contain arguments to the left and right,
# respectively, of <keyword>.
#
# Example usage:
#  function(WXWIDGETS_ADD_RESOURCES outfiles)
#    WX_SPLIT_ARGUMENTS_ON(OPTIONS wxrc_files wxrc_options ${ARGN})
#    ...
#  endfunction()
#
#  WXWIDGETS_ADD_RESOURCES(sources ${xrc_files} OPTIONS -e -o file.C)
#
# NOTE: This is a generic piece of code that should be renamed to
# SPLIT_ARGUMENTS_ON and put in a file serving the same purpose as
# FindPackageStandardArgs.cmake. At the time of this writing
# FindQt4.cmake has a QT4_EXTRACT_OPTIONS, which I basically copied
# here a bit more generalized. So, there are already two find modules
# using this approach.
#
function(WX_SPLIT_ARGUMENTS_ON _keyword _leftvar _rightvar)
  # FIXME: Document that the input variables will be cleared.
  #list(APPEND ${_leftvar}  "")
  #list(APPEND ${_rightvar} "")
  set(${_leftvar}  "")
  set(${_rightvar} "")

  set(_doing_right FALSE)
  foreach(element ${ARGN})
    if("${element}" STREQUAL "${_keyword}")
      set(_doing_right TRUE)
    else()
      if(_doing_right)
        list(APPEND ${_rightvar} "${element}")
      else()
        list(APPEND ${_leftvar} "${element}")
      endif()
    endif()
  endforeach()

  set(${_leftvar}  ${${_leftvar}}  PARENT_SCOPE)
  set(${_rightvar} ${${_rightvar}} PARENT_SCOPE)
endfunction()

#
# WX_GET_DEPENDENCIES_FROM_XML(
#   <depends>
#   <match_pattern>
#   <clean_pattern>
#   <xml_contents>
#   <depends_path>
#   )
#
# FIXME: Add documentation here...
#
function(WX_GET_DEPENDENCIES_FROM_XML
    _depends
    _match_patt
    _clean_patt
    _xml_contents
    _depends_path
    )

  string(REGEX MATCHALL
    ${_match_patt}
    dep_file_list
    "${${_xml_contents}}"
    )
  foreach(dep_file ${dep_file_list})
    string(REGEX REPLACE ${_clean_patt} "" dep_file "${dep_file}")

    # make the file have an absolute path
    if(NOT IS_ABSOLUTE "${dep_file}")
      set(dep_file "${${_depends_path}}/${dep_file}")
    endif()

    # append file to dependency list
    list(APPEND ${_depends} "${dep_file}")
  endforeach()

  set(${_depends} ${${_depends}} PARENT_SCOPE)
endfunction()

#
# WXWIDGETS_ADD_RESOURCES(<sources> <xrc_files>
#                         OPTIONS <options> [NO_CPP_CODE])
#
# Adds a custom command for resource file compilation of the
# <xrc_files> and appends the output files to <sources>.
#
# Example usages:
#   WXWIDGETS_ADD_RESOURCES(sources xrc/main_frame.xrc)
#   WXWIDGETS_ADD_RESOURCES(sources ${xrc_files} OPTIONS -e -o altname.cxx)
#
function(WXWIDGETS_ADD_RESOURCES _outfiles)
  WX_SPLIT_ARGUMENTS_ON(OPTIONS rc_file_list rc_options ${ARGN})

  # Parse files for dependencies.
  set(rc_file_list_abs "")
  set(rc_depends       "")
  foreach(rc_file ${rc_file_list})
    get_filename_component(depends_path ${rc_file} PATH)

    get_filename_component(rc_file_abs ${rc_file} ABSOLUTE)
    list(APPEND rc_file_list_abs "${rc_file_abs}")

    # All files have absolute paths or paths relative to the location
    # of the rc file.
    file(READ "${rc_file_abs}" rc_file_contents)

    # get bitmap/bitmap2 files
    WX_GET_DEPENDENCIES_FROM_XML(
      rc_depends
      "<bitmap[^<]+"
      "^<bitmap[^>]*>"
      rc_file_contents
      depends_path
      )

    # get url files
    WX_GET_DEPENDENCIES_FROM_XML(
      rc_depends
      "<url[^<]+"
      "^<url[^>]*>"
      rc_file_contents
      depends_path
      )

    # get wxIcon files
    WX_GET_DEPENDENCIES_FROM_XML(
      rc_depends
      "<object[^>]*class=\"wxIcon\"[^<]+"
      "^<object[^>]*>"
      rc_file_contents
      depends_path
      )
  endforeach()

  #
  # Parse options.
  #
  # If NO_CPP_CODE option specified, then produce .xrs file rather
  # than a .cpp file (i.e., don't add the default --cpp-code option).
  list(FIND rc_options NO_CPP_CODE index)
  if(index EQUAL -1)
    list(APPEND rc_options --cpp-code)
    # wxrc's default output filename for cpp code.
    set(outfile resource.cpp)
  else()
    list(REMOVE_AT rc_options ${index})
    # wxrc's default output filename for xrs file.
    set(outfile resource.xrs)
  endif()

  # Get output name for use in ADD_CUSTOM_COMMAND.
  # - short option scanning
  list(FIND rc_options -o index)
  if(NOT index EQUAL -1)
    math(EXPR filename_index "${index} + 1")
    list(GET rc_options ${filename_index} outfile)
    #list(REMOVE_AT rc_options ${index} ${filename_index})
  endif()
  # - long option scanning
  string(REGEX MATCH "--output=[^;]*" outfile_opt "${rc_options}")
  if(outfile_opt)
    string(REPLACE "--output=" "" outfile "${outfile_opt}")
  endif()
  #string(REGEX REPLACE "--output=[^;]*;?" "" rc_options "${rc_options}")
  #string(REGEX REPLACE ";$" "" rc_options "${rc_options}")

  if(NOT IS_ABSOLUTE "${outfile}")
    set(outfile "${CMAKE_CURRENT_BINARY_DIR}/${outfile}")
  endif()
  add_custom_command(
    OUTPUT "${outfile}"
    COMMAND ${wxWidgets_wxrc_EXECUTABLE} ${rc_options} ${rc_file_list_abs}
    DEPENDS ${rc_file_list_abs} ${rc_depends}
    )

  # Add generated header to output file list.
  list(FIND rc_options -e short_index)
  list(FIND rc_options --extra-cpp-code long_index)
  if(NOT short_index EQUAL -1 OR NOT long_index EQUAL -1)
    get_filename_component(outfile_ext ${outfile} EXT)
    string(REPLACE "${outfile_ext}" ".h" outfile_header "${outfile}")
    list(APPEND ${_outfiles} "${outfile_header}")
    set_source_files_properties(
      "${outfile_header}" PROPERTIES GENERATED TRUE
      )
  endif()

  # Add generated file to output file list.
  list(APPEND ${_outfiles} "${outfile}")

  set(${_outfiles} ${${_outfiles}} PARENT_SCOPE)
endfunction()
