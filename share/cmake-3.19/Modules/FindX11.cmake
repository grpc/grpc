# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindX11
-------

Find X11 installation

Try to find X11 on UNIX systems. The following values are defined

::

  X11_FOUND        - True if X11 is available
  X11_INCLUDE_DIR  - include directories to use X11
  X11_LIBRARIES    - link against these to use X11

and also the following more fine grained variables and targets:

::

  X11_ICE_INCLUDE_PATH,          X11_ICE_LIB,        X11_ICE_FOUND,        X11::ICE
  X11_SM_INCLUDE_PATH,           X11_SM_LIB,         X11_SM_FOUND,         X11::SM
  X11_X11_INCLUDE_PATH,          X11_X11_LIB,                              X11::X11
  X11_Xaccessrules_INCLUDE_PATH,
  X11_Xaccessstr_INCLUDE_PATH,                       X11_Xaccess_FOUND
  X11_Xau_INCLUDE_PATH,          X11_Xau_LIB,        X11_Xau_FOUND,        X11::Xau
  X11_xcb_INCLUDE_PATH,          X11_xcb_LIB,        X11_xcb_FOUND,        X11::xcb
  X11_X11_xcb_INCLUDE_PATH,      X11_X11_xcb_LIB,    X11_X11_xcb_FOUND,    X11::X11_xcb
  X11_xcb_icccm_INCLUDE_PATH,    X11_xcb_icccm_LIB,  X11_xcb_icccm_FOUND,  X11::xcb_icccm
  X11_xcb_util_INCLUDE_PATH,     X11_xcb_util_LIB,   X11_xcb_util_FOUND,   X11::xcb_util
  X11_xcb_xfixes_INCLUDE_PATH,   X11_xcb_xfixes_LIB, X11_xcb_xfixes_FOUND, X11::xcb_xfixes
  X11_xcb_xkb_INCLUDE_PATH,      X11_xcb_xkb_LIB,    X11_xcb_xkb_FOUND,    X11::xcb_xkb
  X11_Xcomposite_INCLUDE_PATH,   X11_Xcomposite_LIB, X11_Xcomposite_FOUND, X11::Xcomposite
  X11_Xcursor_INCLUDE_PATH,      X11_Xcursor_LIB,    X11_Xcursor_FOUND,    X11::Xcursor
  X11_Xdamage_INCLUDE_PATH,      X11_Xdamage_LIB,    X11_Xdamage_FOUND,    X11::Xdamage
  X11_Xdmcp_INCLUDE_PATH,        X11_Xdmcp_LIB,      X11_Xdmcp_FOUND,      X11::Xdmcp
  X11_Xext_INCLUDE_PATH,         X11_Xext_LIB,       X11_Xext_FOUND,       X11::Xext
  X11_Xxf86misc_INCLUDE_PATH,    X11_Xxf86misc_LIB,  X11_Xxf86misc_FOUND,  X11::Xxf86misc
  X11_Xxf86vm_INCLUDE_PATH,      X11_Xxf86vm_LIB     X11_Xxf86vm_FOUND,    X11::Xxf86vm
  X11_Xfixes_INCLUDE_PATH,       X11_Xfixes_LIB,     X11_Xfixes_FOUND,     X11::Xfixes
  X11_Xft_INCLUDE_PATH,          X11_Xft_LIB,        X11_Xft_FOUND,        X11::Xft
  X11_Xi_INCLUDE_PATH,           X11_Xi_LIB,         X11_Xi_FOUND,         X11::Xi
  X11_Xinerama_INCLUDE_PATH,     X11_Xinerama_LIB,   X11_Xinerama_FOUND,   X11::Xinerama
  X11_Xkb_INCLUDE_PATH,
  X11_Xkblib_INCLUDE_PATH,                           X11_Xkb_FOUND,        X11::Xkb
  X11_xkbcommon_INCLUDE_PATH,    X11_xkbcommon_LIB,  X11_xkbcommon_FOUND,  X11::xkbcommon
  X11_xkbcommon_X11_INCLUDE_PATH,X11_xkbcommon_X11_LIB,X11_xkbcommon_X11_FOUND,X11::xkbcommon_X11
  X11_xkbfile_INCLUDE_PATH,      X11_xkbfile_LIB,    X11_xkbfile_FOUND,    X11::xkbfile
  X11_Xmu_INCLUDE_PATH,          X11_Xmu_LIB,        X11_Xmu_FOUND,        X11::Xmu
  X11_Xpm_INCLUDE_PATH,          X11_Xpm_LIB,        X11_Xpm_FOUND,        X11::Xpm
  X11_Xtst_INCLUDE_PATH,         X11_Xtst_LIB,       X11_Xtst_FOUND,       X11::Xtst
  X11_Xrandr_INCLUDE_PATH,       X11_Xrandr_LIB,     X11_Xrandr_FOUND,     X11::Xrandr
  X11_Xrender_INCLUDE_PATH,      X11_Xrender_LIB,    X11_Xrender_FOUND,    X11::Xrender
  X11_XRes_INCLUDE_PATH,         X11_XRes_LIB,       X11_XRes_FOUND,       X11::XRes
  X11_Xss_INCLUDE_PATH,          X11_Xss_LIB,        X11_Xss_FOUND,        X11::Xss
  X11_Xt_INCLUDE_PATH,           X11_Xt_LIB,         X11_Xt_FOUND,         X11::Xt
  X11_Xutil_INCLUDE_PATH,                            X11_Xutil_FOUND,      X11::Xutil
  X11_Xv_INCLUDE_PATH,           X11_Xv_LIB,         X11_Xv_FOUND,         X11::Xv
  X11_dpms_INCLUDE_PATH,         (in X11_Xext_LIB),  X11_dpms_FOUND
  X11_XShm_INCLUDE_PATH,         (in X11_Xext_LIB),  X11_XShm_FOUND
  X11_Xshape_INCLUDE_PATH,       (in X11_Xext_LIB),  X11_Xshape_FOUND
  X11_XSync_INCLUDE_PATH,        (in X11_Xext_LIB),  X11_XSync_FOUND
  X11_Xaw_INCLUDE_PATH,          X11_Xaw_LIB         X11_Xaw_FOUND         X11::Xaw
#]=======================================================================]

if (UNIX)
  set(X11_FOUND 0)
  # X11 is never a framework and some header files may be
  # found in tcl on the mac
  set(CMAKE_FIND_FRAMEWORK_SAVE ${CMAKE_FIND_FRAMEWORK})
  set(CMAKE_FIND_FRAMEWORK NEVER)
  set(CMAKE_REQUIRED_QUIET_SAVE ${CMAKE_REQUIRED_QUIET})
  set(CMAKE_REQUIRED_QUIET ${X11_FIND_QUIETLY})
  set(X11_INC_SEARCH_PATH
    /usr/pkg/xorg/include
    /usr/X11R6/include
    /usr/X11R7/include
    /usr/include/X11
    /usr/openwin/include
    /usr/openwin/share/include
    /opt/graphics/OpenGL/include
    /opt/X11/include
  )

  set(X11_LIB_SEARCH_PATH
    /usr/pkg/xorg/lib
    /usr/X11R6/lib
    /usr/X11R7/lib
    /usr/openwin/lib
    /opt/X11/lib
  )

  find_path(X11_X11_INCLUDE_PATH X11/X.h                             ${X11_INC_SEARCH_PATH})
  find_path(X11_Xlib_INCLUDE_PATH X11/Xlib.h                         ${X11_INC_SEARCH_PATH})

  # Look for includes; keep the list sorted by name of the cmake *_INCLUDE_PATH
  # variable (which doesn't need to match the include file name).

  # Solaris lacks XKBrules.h, so we should skip kxkbd there.
  find_path(X11_ICE_INCLUDE_PATH X11/ICE/ICE.h                       ${X11_INC_SEARCH_PATH})
  find_path(X11_SM_INCLUDE_PATH X11/SM/SM.h                          ${X11_INC_SEARCH_PATH})
  find_path(X11_Xaccessrules_INCLUDE_PATH X11/extensions/XKBrules.h  ${X11_INC_SEARCH_PATH})
  find_path(X11_Xaccessstr_INCLUDE_PATH X11/extensions/XKBstr.h      ${X11_INC_SEARCH_PATH})
  find_path(X11_Xau_INCLUDE_PATH X11/Xauth.h                         ${X11_INC_SEARCH_PATH})
  find_path(X11_Xaw_INCLUDE_PATH X11/Xaw/Intrinsic.h                 ${X11_INC_SEARCH_PATH})
  find_path(X11_xcb_INCLUDE_PATH xcb/xcb.h                           ${X11_INC_SEARCH_PATH})
  find_path(X11_X11_xcb_INCLUDE_PATH X11/Xlib-xcb.h                  ${X11_INC_SEARCH_PATH})
  find_path(X11_xcb_icccm_INCLUDE_PATH xcb/xcb_icccm.h               ${X11_INC_SEARCH_PATH})
  find_path(X11_xcb_util_INCLUDE_PATH xcb/xcb_aux.h                  ${X11_INC_SEARCH_PATH})
  find_path(X11_xcb_xfixes_INCLUDE_PATH xcb/xfixes.h                 ${X11_INC_SEARCH_PATH})
  find_path(X11_Xcomposite_INCLUDE_PATH X11/extensions/Xcomposite.h  ${X11_INC_SEARCH_PATH})
  find_path(X11_Xcursor_INCLUDE_PATH X11/Xcursor/Xcursor.h           ${X11_INC_SEARCH_PATH})
  find_path(X11_Xdamage_INCLUDE_PATH X11/extensions/Xdamage.h        ${X11_INC_SEARCH_PATH})
  find_path(X11_Xdmcp_INCLUDE_PATH X11/Xdmcp.h                       ${X11_INC_SEARCH_PATH})
  find_path(X11_Xext_INCLUDE_PATH X11/extensions/Xext.h              ${X11_INC_SEARCH_PATH})
  find_path(X11_dpms_INCLUDE_PATH X11/extensions/dpms.h              ${X11_INC_SEARCH_PATH})
  find_path(X11_Xxf86misc_INCLUDE_PATH X11/extensions/xf86misc.h     ${X11_INC_SEARCH_PATH})
  find_path(X11_Xxf86vm_INCLUDE_PATH X11/extensions/xf86vmode.h      ${X11_INC_SEARCH_PATH})
  find_path(X11_Xfixes_INCLUDE_PATH X11/extensions/Xfixes.h          ${X11_INC_SEARCH_PATH})
  find_path(X11_Xft_INCLUDE_PATH X11/Xft/Xft.h                       ${X11_INC_SEARCH_PATH})
  find_path(X11_Xi_INCLUDE_PATH X11/extensions/XInput.h              ${X11_INC_SEARCH_PATH})
  find_path(X11_Xinerama_INCLUDE_PATH X11/extensions/Xinerama.h      ${X11_INC_SEARCH_PATH})
  find_path(X11_Xkb_INCLUDE_PATH X11/extensions/XKB.h                ${X11_INC_SEARCH_PATH})
  find_path(X11_xkbcommon_INCLUDE_PATH xkbcommon/xkbcommon.h         ${X11_INC_SEARCH_PATH})
  find_path(X11_xkbcommon_X11_INCLUDE_PATH xkbcommon/xkbcommon-x11.h ${X11_INC_SEARCH_PATH})
  find_path(X11_Xkblib_INCLUDE_PATH X11/XKBlib.h                     ${X11_INC_SEARCH_PATH})
  find_path(X11_xkbfile_INCLUDE_PATH X11/extensions/XKBfile.h        ${X11_INC_SEARCH_PATH})
  find_path(X11_Xmu_INCLUDE_PATH X11/Xmu/Xmu.h                       ${X11_INC_SEARCH_PATH})
  find_path(X11_Xpm_INCLUDE_PATH X11/xpm.h                           ${X11_INC_SEARCH_PATH})
  find_path(X11_Xtst_INCLUDE_PATH X11/extensions/XTest.h             ${X11_INC_SEARCH_PATH})
  find_path(X11_XShm_INCLUDE_PATH X11/extensions/XShm.h              ${X11_INC_SEARCH_PATH})
  find_path(X11_Xrandr_INCLUDE_PATH X11/extensions/Xrandr.h          ${X11_INC_SEARCH_PATH})
  find_path(X11_Xrender_INCLUDE_PATH X11/extensions/Xrender.h        ${X11_INC_SEARCH_PATH})
  find_path(X11_XRes_INCLUDE_PATH X11/extensions/XRes.h              ${X11_INC_SEARCH_PATH})
  find_path(X11_Xss_INCLUDE_PATH X11/extensions/scrnsaver.h          ${X11_INC_SEARCH_PATH})
  find_path(X11_Xshape_INCLUDE_PATH X11/extensions/shape.h           ${X11_INC_SEARCH_PATH})
  find_path(X11_Xutil_INCLUDE_PATH X11/Xutil.h                       ${X11_INC_SEARCH_PATH})
  find_path(X11_Xt_INCLUDE_PATH X11/Intrinsic.h                      ${X11_INC_SEARCH_PATH})
  find_path(X11_Xv_INCLUDE_PATH X11/extensions/Xvlib.h               ${X11_INC_SEARCH_PATH})
  find_path(X11_XSync_INCLUDE_PATH X11/extensions/sync.h             ${X11_INC_SEARCH_PATH})



  # Backwards compatibility.
  set(X11_Xinput_INCLUDE_PATH "${X11_Xi_INCLUDE_PATH}")
  set(X11_xf86misc_INCLUDE_PATH "${X11_Xxf86misc_INCLUDE_PATH}")
  set(X11_xf86vmode_INCLUDE_PATH "${X11_Xxf8vm_INCLUDE_PATH}")
  set(X11_Xkbfile_INCLUDE_PATH "${X11_xkbfile_INCLUDE_PATH}")
  set(X11_XTest_INCLUDE_PATH "${X11_Xtst_INCLUDE_PATH}")
  set(X11_Xscreensaver_INCLUDE_PATH "${X11_Xss_INCLUDE_PATH}")

  find_library(X11_X11_LIB X11               ${X11_LIB_SEARCH_PATH})

  # Find additional X libraries. Keep list sorted by library name.
  find_library(X11_ICE_LIB ICE               ${X11_LIB_SEARCH_PATH})
  find_library(X11_SM_LIB SM                 ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xau_LIB Xau               ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xaw_LIB Xaw               ${X11_LIB_SEARCH_PATH})
  find_library(X11_xcb_LIB xcb               ${X11_LIB_SEARCH_PATH})
  find_library(X11_X11_xcb_LIB X11-xcb       ${X11_LIB_SEARCH_PATH})
  find_library(X11_xcb_icccm_LIB xcb-icccm   ${X11_LIB_SEARCH_PATH})
  find_library(X11_xcb_util_LIB xcb-util     ${X11_LIB_SEARCH_PATH})
  find_library(X11_xcb_xfixes_LIB xcb-xfixes ${X11_LIB_SEARCH_PATH})
  find_library(X11_xcb_xkb_LIB xcb-xkb       ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xcomposite_LIB Xcomposite ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xcursor_LIB Xcursor       ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xdamage_LIB Xdamage       ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xdmcp_LIB Xdmcp           ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xext_LIB Xext             ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xfixes_LIB Xfixes         ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xft_LIB Xft               ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xi_LIB Xi                 ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xinerama_LIB Xinerama     ${X11_LIB_SEARCH_PATH})
  find_library(X11_xkbcommon_LIB xkbcommon   ${X11_LIB_SEARCH_PATH})
  find_library(X11_xkbcommon_X11_LIB xkbcommon-x11   ${X11_LIB_SEARCH_PATH})
  find_library(X11_xkbfile_LIB xkbfile       ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xmu_LIB Xmu               ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xpm_LIB Xpm               ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xrandr_LIB Xrandr         ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xrender_LIB Xrender       ${X11_LIB_SEARCH_PATH})
  find_library(X11_XRes_LIB XRes             ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xss_LIB Xss               ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xt_LIB Xt                 ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xtst_LIB Xtst             ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xv_LIB Xv                 ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xxf86misc_LIB Xxf86misc   ${X11_LIB_SEARCH_PATH})
  find_library(X11_Xxf86vm_LIB Xxf86vm       ${X11_LIB_SEARCH_PATH})

  # Backwards compatibility.
  set(X11_Xinput_LIB "${X11_Xi_LIB}")
  set(X11_Xkbfile_LIB "${X11_xkbfile_LIB}")
  set(X11_XTest_LIB "${X11_Xtst_LIB}")
  set(X11_Xscreensaver_LIB "${X11_Xss_LIB}")

  set(X11_LIBRARY_DIR "")
  if(X11_X11_LIB)
    get_filename_component(X11_LIBRARY_DIR ${X11_X11_LIB} PATH)
  endif()

  set(X11_INCLUDE_DIR) # start with empty list
  if(X11_X11_INCLUDE_PATH)
    list(APPEND X11_INCLUDE_DIR ${X11_X11_INCLUDE_PATH})
  endif()

  if(X11_Xlib_INCLUDE_PATH)
    list(APPEND X11_INCLUDE_DIR ${X11_Xlib_INCLUDE_PATH})
  endif()

  if(X11_Xutil_INCLUDE_PATH)
    set(X11_Xutil_FOUND TRUE)
    list(APPEND X11_INCLUDE_DIR ${X11_Xutil_INCLUDE_PATH})
  endif()

  if(X11_Xshape_INCLUDE_PATH)
    set(X11_Xshape_FOUND TRUE)
    list(APPEND X11_INCLUDE_DIR ${X11_Xshape_INCLUDE_PATH})
  endif()

  set(X11_LIBRARIES) # start with empty list
  if(X11_X11_LIB)
    list(APPEND X11_LIBRARIES ${X11_X11_LIB})
  endif()

  if(X11_Xext_LIB)
    set(X11_Xext_FOUND TRUE)
    list(APPEND X11_LIBRARIES ${X11_Xext_LIB})
  endif()

  if(X11_Xt_LIB AND X11_Xt_INCLUDE_PATH)
    set(X11_Xt_FOUND TRUE)
  endif()

  if(X11_Xft_LIB AND X11_Xft_INCLUDE_PATH)
    find_package(Freetype QUIET)
    find_package(Fontconfig QUIET)
    if (FREETYPE_FOUND AND Fontconfig_FOUND)
      set(X11_Xft_FOUND TRUE)
    endif ()
    list(APPEND X11_INCLUDE_DIR ${X11_Xft_INCLUDE_PATH})
  endif()

  if(X11_Xv_LIB AND X11_Xv_INCLUDE_PATH)
    set(X11_Xv_FOUND TRUE)
    list(APPEND X11_INCLUDE_DIR ${X11_Xv_INCLUDE_PATH})
  endif()

  if (X11_Xau_LIB AND X11_Xau_INCLUDE_PATH)
    set(X11_Xau_FOUND TRUE)
  endif ()

  if (X11_xcb_LIB AND X11_xcb_INCLUDE_PATH)
    set(X11_xcb_FOUND TRUE)
  endif ()

  if (X11_X11_xcb_LIB AND X11_X11_xcb_INCLUDE_PATH)
    set(X11_X11_xcb_FOUND TRUE)
  endif ()

  if (X11_xcb_icccm_LIB AND X11_xcb_icccm_INCLUDE_PATH)
    set(X11_xcb_icccm_FOUND TRUE)
  endif ()

  if (X11_xcb_util_LIB AND X11_xcb_util_INCLUDE_PATH)
    set(X11_xcb_util_FOUND TRUE)
  endif ()

  if (X11_xcb_xfixes_LIB)
    set(X11_xcb_xfixes_FOUND TRUE)
  endif ()

  if (X11_xcb_xkb_LIB)
    set(X11_xcb_xkb_FOUND TRUE)
  endif ()

  if (X11_Xdmcp_INCLUDE_PATH AND X11_Xdmcp_LIB)
      set(X11_Xdmcp_FOUND TRUE)
      list(APPEND X11_INCLUDE_DIR ${X11_Xdmcp_INCLUDE_PATH})
  endif ()

  if (X11_Xaccessrules_INCLUDE_PATH AND X11_Xaccessstr_INCLUDE_PATH)
      set(X11_Xaccess_FOUND TRUE)
      set(X11_Xaccess_INCLUDE_PATH ${X11_Xaccessstr_INCLUDE_PATH})
      list(APPEND X11_INCLUDE_DIR ${X11_Xaccess_INCLUDE_PATH})
  endif ()

  if (X11_Xpm_INCLUDE_PATH AND X11_Xpm_LIB)
      set(X11_Xpm_FOUND TRUE)
      list(APPEND X11_INCLUDE_DIR ${X11_Xpm_INCLUDE_PATH})
  endif ()

  if (X11_Xcomposite_INCLUDE_PATH AND X11_Xcomposite_LIB)
     set(X11_Xcomposite_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xcomposite_INCLUDE_PATH})
  endif ()

  if (X11_Xdamage_INCLUDE_PATH AND X11_Xdamage_LIB)
     set(X11_Xdamage_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xdamage_INCLUDE_PATH})
  endif ()

  if (X11_XShm_INCLUDE_PATH)
     set(X11_XShm_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_XShm_INCLUDE_PATH})
  endif ()

  if (X11_Xtst_INCLUDE_PATH AND X11_Xtst_LIB)
      set(X11_Xtst_FOUND TRUE)
      # Backwards compatibility.
      set(X11_XTest_FOUND TRUE)
      list(APPEND X11_INCLUDE_DIR ${X11_Xtst_INCLUDE_PATH})
  endif ()

  if (X11_Xi_INCLUDE_PATH AND X11_Xi_LIB)
     set(X11_Xi_FOUND TRUE)
     # Backwards compatibility.
     set(X11_Xinput_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xi_INCLUDE_PATH})
  endif ()

  if (X11_Xinerama_INCLUDE_PATH AND X11_Xinerama_LIB)
     set(X11_Xinerama_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xinerama_INCLUDE_PATH})
  endif ()

  if (X11_Xfixes_INCLUDE_PATH AND X11_Xfixes_LIB)
     set(X11_Xfixes_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xfixes_INCLUDE_PATH})
  endif ()

  if (X11_Xrender_INCLUDE_PATH AND X11_Xrender_LIB)
     set(X11_Xrender_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xrender_INCLUDE_PATH})
  endif ()

  if (X11_XRes_INCLUDE_PATH AND X11_XRes_LIB)
     set(X11_XRes_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_XRes_INCLUDE_PATH})
  endif ()

  if (X11_Xrandr_INCLUDE_PATH AND X11_Xrandr_LIB)
     set(X11_Xrandr_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xrandr_INCLUDE_PATH})
  endif ()

  if (X11_Xxf86misc_INCLUDE_PATH AND X11_Xxf86misc_LIB)
     set(X11_Xxf86misc_FOUND TRUE)
     # Backwards compatibility.
     set(X11_xf86misc_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xxf86misc_INCLUDE_PATH})
  endif ()

  if (X11_Xxf86vm_INCLUDE_PATH AND X11_Xxf86vm_LIB)
     set(X11_Xxf86vm_FOUND TRUE)
     # Backwards compatibility.
     set(X11_xf86vmode_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xxf86vm_INCLUDE_PATH})
  endif ()

  if (X11_Xcursor_INCLUDE_PATH AND X11_Xcursor_LIB)
     set(X11_Xcursor_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xcursor_INCLUDE_PATH})
  endif ()

  if (X11_Xss_INCLUDE_PATH AND X11_Xss_LIB)
     set(X11_Xss_FOUND TRUE)
     set(X11_Xscreensaver_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xss_INCLUDE_PATH})
  endif ()

  if (X11_dpms_INCLUDE_PATH)
     set(X11_dpms_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_dpms_INCLUDE_PATH})
  endif ()

  if (X11_Xkb_INCLUDE_PATH AND X11_Xkblib_INCLUDE_PATH AND X11_Xlib_INCLUDE_PATH)
     set(X11_Xkb_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xkb_INCLUDE_PATH} )
  endif ()

  if (X11_xkbcommon_INCLUDE_PATH AND X11_xkbcommon_LIB)
     set(X11_xkbcommon_FOUND TRUE)
  endif ()

  if (X11_xkbcommon_X11_INCLUDE_PATH AND X11_xkbcommon_X11_LIB)
     set(X11_xkbcommon_X11_FOUND TRUE)
  endif ()

  if (X11_xkbfile_INCLUDE_PATH AND X11_xkbfile_LIB AND X11_Xlib_INCLUDE_PATH)
     set(X11_xkbfile_FOUND TRUE)
     # Backwards compatibility.
     set(X11_Xkbfile_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_xkbfile_INCLUDE_PATH} )
  endif ()

  if (X11_Xmu_INCLUDE_PATH AND X11_Xmu_LIB)
     set(X11_Xmu_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_Xmu_INCLUDE_PATH})
  endif ()

  if (X11_XSync_INCLUDE_PATH)
     set(X11_XSync_FOUND TRUE)
     list(APPEND X11_INCLUDE_DIR ${X11_XSync_INCLUDE_PATH})
  endif ()

  if(X11_ICE_LIB AND X11_ICE_INCLUDE_PATH)
     set(X11_ICE_FOUND TRUE)
  endif()

  if(X11_SM_LIB AND X11_SM_INCLUDE_PATH)
     set(X11_SM_FOUND TRUE)
  endif()

  if(X11_Xaw_LIB AND X11_Xaw_INCLUDE_PATH)
      set(X11_Xaw_FOUND TRUE)
  endif()

  # Most of the X11 headers will be in the same directories, avoid
  # creating a huge list of duplicates.
  if (X11_INCLUDE_DIR)
     list(REMOVE_DUPLICATES X11_INCLUDE_DIR)
  endif ()

  # Deprecated variable for backwards compatibility with CMake 1.4
  if (X11_X11_INCLUDE_PATH AND X11_LIBRARIES)
    set(X11_FOUND 1)
  endif ()

  include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
  if (CMAKE_FIND_PACKAGE_NAME STREQUAL "FLTK")
    # FindFLTK include()'s this module. It's an old pattern, but rather than
    # trying to suppress this from outside the module (which is then sensitive
    # to the contents, detect the case in this module and suppress it
    # explicitly.
    set(FPHSA_NAME_MISMATCHED 1)
  endif ()
  find_package_handle_standard_args(X11
    REQUIRED_VARS X11_X11_INCLUDE_PATH X11_X11_LIB
    HANDLE_COMPONENTS)
  unset(FPHSA_NAME_MISMATCHED)

  if(X11_FOUND)
    include(${CMAKE_CURRENT_LIST_DIR}/CheckFunctionExists.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/CheckLibraryExists.cmake)

    # Translated from an autoconf-generated configure script.
    # See libs.m4 in autoconf's m4 directory.
    if($ENV{ISC} MATCHES "^yes$")
      set(X11_X_EXTRA_LIBS -lnsl_s -linet)
    else()
      set(X11_X_EXTRA_LIBS "")

      # See if XOpenDisplay in X11 works by itself.
      check_library_exists("${X11_LIBRARIES}" "XOpenDisplay" "${X11_LIBRARY_DIR}" X11_LIB_X11_SOLO)
      if(NOT X11_LIB_X11_SOLO)
        # Find library needed for dnet_ntoa.
        check_library_exists("dnet" "dnet_ntoa" "" X11_LIB_DNET_HAS_DNET_NTOA)
        if (X11_LIB_DNET_HAS_DNET_NTOA)
          list(APPEND X11_X_EXTRA_LIBS -ldnet)
        else ()
          check_library_exists("dnet_stub" "dnet_ntoa" "" X11_LIB_DNET_STUB_HAS_DNET_NTOA)
          if (X11_LIB_DNET_STUB_HAS_DNET_NTOA)
            list(APPEND X11_X_EXTRA_LIBS -ldnet_stub)
          endif ()
        endif ()
      endif()

      # Find library needed for gethostbyname.
      check_function_exists("gethostbyname" CMAKE_HAVE_GETHOSTBYNAME)
      if(NOT CMAKE_HAVE_GETHOSTBYNAME)
        check_library_exists("nsl" "gethostbyname" "" CMAKE_LIB_NSL_HAS_GETHOSTBYNAME)
        if (CMAKE_LIB_NSL_HAS_GETHOSTBYNAME)
          list(APPEND X11_X_EXTRA_LIBS -lnsl)
        else ()
          check_library_exists("bsd" "gethostbyname" "" CMAKE_LIB_BSD_HAS_GETHOSTBYNAME)
          if (CMAKE_LIB_BSD_HAS_GETHOSTBYNAME)
            list(APPEND X11_X_EXTRA_LIBS -lbsd)
          endif ()
        endif ()
      endif()

      # Find library needed for connect.
      check_function_exists("connect" CMAKE_HAVE_CONNECT)
      if(NOT CMAKE_HAVE_CONNECT)
        check_library_exists("socket" "connect" "" CMAKE_LIB_SOCKET_HAS_CONNECT)
        if (CMAKE_LIB_SOCKET_HAS_CONNECT)
          list(INSERT X11_X_EXTRA_LIBS 0 -lsocket)
        endif ()
      endif()

      # Find library needed for remove.
      check_function_exists("remove" CMAKE_HAVE_REMOVE)
      if(NOT CMAKE_HAVE_REMOVE)
        check_library_exists("posix" "remove" "" CMAKE_LIB_POSIX_HAS_REMOVE)
        if (CMAKE_LIB_POSIX_HAS_REMOVE)
          list(APPEND X11_X_EXTRA_LIBS -lposix)
        endif ()
      endif()

      # Find library needed for shmat.
      check_function_exists("shmat" CMAKE_HAVE_SHMAT)
      if(NOT CMAKE_HAVE_SHMAT)
        check_library_exists("ipc" "shmat" "" CMAKE_LIB_IPS_HAS_SHMAT)
        if (CMAKE_LIB_IPS_HAS_SHMAT)
          list(APPEND X11_X_EXTRA_LIBS -lipc)
        endif ()
      endif()
    endif()

    if (X11_ICE_FOUND)
      check_library_exists("ICE" "IceConnectionNumber" "${X11_LIBRARY_DIR}"
                            CMAKE_LIB_ICE_HAS_ICECONNECTIONNUMBER)
      if(CMAKE_LIB_ICE_HAS_ICECONNECTIONNUMBER)
        set (X11_X_PRE_LIBS ${X11_ICE_LIB})
        if(X11_SM_LIB)
          list(INSERT X11_X_PRE_LIBS 0 ${X11_SM_LIB})
        endif()
      endif()
    endif ()

    # Build the final list of libraries.
    set(X11_LIBRARIES ${X11_X_PRE_LIBS} ${X11_LIBRARIES} ${X11_X_EXTRA_LIBS})

    if (NOT TARGET X11::X11)
      add_library(X11::X11 UNKNOWN IMPORTED)
      set_target_properties(X11::X11 PROPERTIES
        IMPORTED_LOCATION "${X11_X11_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${X11_X11_INCLUDE_PATH}")
    endif ()
  endif ()

  if (X11_ICE_FOUND AND NOT TARGET X11::ICE)
    add_library(X11::ICE UNKNOWN IMPORTED)
    set_target_properties(X11::ICE PROPERTIES
      IMPORTED_LOCATION "${X11_ICE_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_ICE_INCLUDE_PATH}")
  endif ()

  if (X11_SM_FOUND AND NOT TARGET X11::SM)
    add_library(X11::SM UNKNOWN IMPORTED)
    set_target_properties(X11::SM PROPERTIES
      IMPORTED_LOCATION "${X11_SM_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_SM_INCLUDE_PATH}")
  endif ()

  if (X11_Xau_FOUND AND NOT TARGET X11::Xau)
    add_library(X11::Xau UNKNOWN IMPORTED)
    set_target_properties(X11::Xau PROPERTIES
      IMPORTED_LOCATION "${X11_Xau_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xau_INCLUDE_PATH}")
  endif ()

  if (X11_Xaw_FOUND AND NOT TARGET X11::Xaw)
    add_library(X11::Xaw UNKNOWN IMPORTED)
    set_target_properties(X11::Xaw PROPERTIES
      IMPORTED_LOCATION "${X11_Xaw_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xaw_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xext;X11::Xmu;X11::Xt;X11::Xpm;X11::X11")
  endif ()

  if (X11_xcb_FOUND AND NOT TARGET X11::xcb)
    add_library(X11::xcb UNKNOWN IMPORTED)
    set_target_properties(X11::xcb PROPERTIES
      IMPORTED_LOCATION "${X11_xcb_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_xcb_INCLUDE_PATH}")
  endif ()

  if (X11_X11_xcb_FOUND AND NOT TARGET X11::X11_xcb)
    add_library(X11::X11_xcb UNKNOWN IMPORTED)
    set_target_properties(X11::X11_xcb PROPERTIES
      IMPORTED_LOCATION "${X11_X11_xcb_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_X11_xcb_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::xcb;X11::X11")
  endif ()

  if (X11_xcb_icccm_FOUND AND NOT TARGET X11::xcb_icccm)
    add_library(X11::xcb_icccm UNKNOWN IMPORTED)
    set_target_properties(X11::xcb_icccm PROPERTIES
      IMPORTED_LOCATION "${X11_xcb_icccm_LIB}"
      INTERFACE_LINK_LIBRARIES "X11::xcb")
  endif ()

  if (X11_xcb_util_FOUND AND NOT TARGET X11::xcb_util)
    add_library(X11::xcb_util UNKNOWN IMPORTED)
    set_target_properties(X11::xcb_util PROPERTIES
      IMPORTED_LOCATION "${X11_xcb_util_LIB}"
      INTERFACE_LINK_LIBRARIES "X11::xcb")
  endif ()

  if (X11_xcb_xfixes_FOUND AND NOT TARGET X11::xcb_xfixes)
    add_library(X11::xcb_xfixes UNKNOWN IMPORTED)
    set_target_properties(X11::xcb_xfixes PROPERTIES
      IMPORTED_LOCATION "${X11_xcb_xfixes_LIB}"
      INTERFACE_LINK_LIBRARIES "X11::xcb")
  endif ()

  if (X11_xcb_xkb_FOUND AND NOT TARGET X11::xcb_xkb)
    add_library(X11::xcb_xkb UNKNOWN IMPORTED)
    set_target_properties(X11::xcb_xkb PROPERTIES
      IMPORTED_LOCATION "${X11_xcb_xkb_LIB}"
      INTERFACE_LINK_LIBRARIES "X11::xcb")
  endif ()

  if (X11_Xcomposite_FOUND AND NOT TARGET X11::Xcomposite)
    add_library(X11::Xcomposite UNKNOWN IMPORTED)
    set_target_properties(X11::Xcomposite PROPERTIES
      IMPORTED_LOCATION "${X11_Xcomposite_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xcomposite_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::X11")
  endif ()

  if (X11_Xcursor_FOUND AND NOT TARGET X11::Xcursor)
    add_library(X11::Xcursor UNKNOWN IMPORTED)
    set_target_properties(X11::Xcursor PROPERTIES
      IMPORTED_LOCATION "${X11_Xcursor_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xcursor_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xrender;X11::Xfixes;X11::X11")
  endif ()

  if (X11_Xdamage_FOUND AND NOT TARGET X11::Xdamage)
    add_library(X11::Xdamage UNKNOWN IMPORTED)
    set_target_properties(X11::Xdamage PROPERTIES
      IMPORTED_LOCATION "${X11_Xdamage_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xdamage_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xfixes;X11::X11")
  endif ()

  if (X11_Xdmcp_FOUND AND NOT TARGET X11::Xdmcp)
    add_library(X11::Xdmcp UNKNOWN IMPORTED)
    set_target_properties(X11::Xdmcp PROPERTIES
      IMPORTED_LOCATION "${X11_Xdmcp_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xdmcp_INCLUDE_PATH}")
  endif ()

  if (X11_Xext_FOUND AND NOT TARGET X11::Xext)
    add_library(X11::Xext UNKNOWN IMPORTED)
    set_target_properties(X11::Xext PROPERTIES
      IMPORTED_LOCATION "${X11_Xext_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xext_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::X11")
  endif ()

  if (X11_Xxf86misc_FOUND AND NOT TARGET X11::Xxf86misc)
    add_library(X11::Xxf86misc UNKNOWN IMPORTED)
    set_target_properties(X11::Xxf86misc PROPERTIES
      IMPORTED_LOCATION "${X11_Xxf86misc_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xxf86misc_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::X11;X11::Xext")
  endif ()

  if (X11_Xxf86vm_FOUND AND NOT TARGET X11::Xxf86vm)
    add_library(X11::Xxf86vm UNKNOWN IMPORTED)
    set_target_properties(X11::Xxf86vm PROPERTIES
      IMPORTED_LOCATION "${X11_Xxf86vm_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xxf86vm_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::X11;X11::Xext")
  endif ()

  if (X11_Xfixes_FOUND AND NOT TARGET X11::Xfixes)
    add_library(X11::Xfixes UNKNOWN IMPORTED)
    set_target_properties(X11::Xfixes PROPERTIES
      IMPORTED_LOCATION "${X11_Xfixes_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xfixes_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::X11")
  endif ()

  if (X11_Xft_FOUND AND NOT TARGET X11::Xft)
    add_library(X11::Xft UNKNOWN IMPORTED)
    set_target_properties(X11::Xft PROPERTIES
      IMPORTED_LOCATION "${X11_Xft_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xft_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xrender;X11::X11;Fontconfig::Fontconfig;Freetype::Freetype")
  endif ()

  if (X11_Xi_FOUND AND NOT TARGET X11::Xi)
    add_library(X11::Xi UNKNOWN IMPORTED)
    set_target_properties(X11::Xi PROPERTIES
      IMPORTED_LOCATION "${X11_Xi_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xi_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xext;X11::X11")
  endif ()

  if (X11_Xinerama_FOUND AND NOT TARGET X11::Xinerama)
    add_library(X11::Xinerama UNKNOWN IMPORTED)
    set_target_properties(X11::Xinerama PROPERTIES
      IMPORTED_LOCATION "${X11_Xinerama_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xinerama_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xext;X11::X11")
  endif ()

  if (X11_Xkb_FOUND AND NOT TARGET X11::Xkb)
    add_library(X11::Xkb INTERFACE IMPORTED)
    set_target_properties(X11::Xkb PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xkb_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::X11")
  endif ()

  if (X11_xkbcommon_FOUND AND NOT TARGET X11::xkbcommon)
    add_library(X11::xkbcommon UNKNOWN IMPORTED)
    set_target_properties(X11::xkbcommon PROPERTIES
      IMPORTED_LOCATION "${X11_xkbcommon_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_xkbcommon_INCLUDE_PATH}")
  endif ()

  if (X11_xkbcommon_X11_FOUND AND NOT TARGET X11::xkbcommon_X11)
    add_library(X11::xkbcommon_X11 UNKNOWN IMPORTED)
    set_target_properties(X11::xkbcommon_X11 PROPERTIES
      IMPORTED_LOCATION "${X11_xkbcommon_X11_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_xkbcommon_X11_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::X11;X11::xkbcommon")
  endif ()

  if (X11_xkbfile_FOUND AND NOT TARGET X11::xkbfile)
    add_library(X11::xkbfile UNKNOWN IMPORTED)
    set_target_properties(X11::xkbfile PROPERTIES
      IMPORTED_LOCATION "${X11_xkbfile_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_xkbfile_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::X11")
  endif ()

  if (X11_Xmu_FOUND AND NOT TARGET X11::Xmu)
    add_library(X11::Xmu UNKNOWN IMPORTED)
    set_target_properties(X11::Xmu PROPERTIES
      IMPORTED_LOCATION "${X11_Xmu_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xmu_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xt;X11::Xext;X11::X11")
  endif ()

  if (X11_Xpm_FOUND AND NOT TARGET X11::Xpm)
    add_library(X11::Xpm UNKNOWN IMPORTED)
    set_target_properties(X11::Xpm PROPERTIES
      IMPORTED_LOCATION "${X11_Xpm_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xpm_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::X11")
  endif ()

  if (X11_Xtst_FOUND AND NOT TARGET X11::Xtst)
    add_library(X11::Xtst UNKNOWN IMPORTED)
    set_target_properties(X11::Xtst PROPERTIES
      IMPORTED_LOCATION "${X11_Xtst_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xtst_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xi;X11::Xext;X11::X11")
  endif ()

  if (X11_Xrandr_FOUND AND NOT TARGET X11::Xrandr)
    add_library(X11::Xrandr UNKNOWN IMPORTED)
    set_target_properties(X11::Xrandr PROPERTIES
      IMPORTED_LOCATION "${X11_Xrandr_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xrandr_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xrender;X11::Xext;X11::X11")
  endif ()

  if (X11_Xrender_FOUND AND NOT TARGET X11::Xrender)
    add_library(X11::Xrender UNKNOWN IMPORTED)
    set_target_properties(X11::Xrender PROPERTIES
      IMPORTED_LOCATION "${X11_Xrender_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xrender_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::X11")
  endif ()

  if (X11_XRes_FOUND AND NOT TARGET X11::XRes)
    add_library(X11::XRes UNKNOWN IMPORTED)
    set_target_properties(X11::XRes PROPERTIES
      IMPORTED_LOCATION "${X11_XRes_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_XRes_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xext;X11::X11")
  endif ()

  if (X11_Xss_FOUND AND NOT TARGET X11::Xss)
    add_library(X11::Xss UNKNOWN IMPORTED)
    set_target_properties(X11::Xss PROPERTIES
      IMPORTED_LOCATION "${X11_Xss_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xss_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xext;X11::X11")
  endif ()

  if (X11_Xt_FOUND AND NOT TARGET X11::Xt)
    add_library(X11::Xt UNKNOWN IMPORTED)
    set_target_properties(X11::Xt PROPERTIES
      IMPORTED_LOCATION "${X11_Xt_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xt_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::ICE;X11::SM;X11::X11")
  endif ()

  if (X11_Xutil_FOUND AND NOT TARGET X11::Xutil)
    add_library(X11::Xutil INTERFACE IMPORTED)
    set_target_properties(X11::Xutil PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xutil_INCLUDE_PATH}"
      # libX11 contains the implementations for functions in the Xutil.h
      # header.
      INTERFACE_LINK_LIBRARIES "X11::X11")
  endif ()

  if (X11_Xv_FOUND AND NOT TARGET X11::Xv)
    add_library(X11::Xv UNKNOWN IMPORTED)
    set_target_properties(X11::Xv PROPERTIES
      IMPORTED_LOCATION "${X11_Xv_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${X11_Xv_INCLUDE_PATH}"
      INTERFACE_LINK_LIBRARIES "X11::Xext;X11::X11")
  endif ()

  mark_as_advanced(
    X11_X11_INCLUDE_PATH
    X11_X11_LIB
    X11_Xext_INCLUDE_PATH
    X11_Xext_LIB
    X11_Xau_LIB
    X11_Xau_INCLUDE_PATH
    X11_xcb_LIB
    X11_xcb_INCLUDE_PATH
    X11_xcb_xkb_LIB
    X11_X11_xcb_LIB
    X11_X11_xcb_INCLUDE_PATH
    X11_Xlib_INCLUDE_PATH
    X11_Xutil_INCLUDE_PATH
    X11_Xcomposite_INCLUDE_PATH
    X11_Xcomposite_LIB
    X11_Xfixes_LIB
    X11_Xfixes_INCLUDE_PATH
    X11_Xrandr_LIB
    X11_Xrandr_INCLUDE_PATH
    X11_Xdamage_LIB
    X11_Xdamage_INCLUDE_PATH
    X11_Xrender_LIB
    X11_Xrender_INCLUDE_PATH
    X11_XRes_LIB
    X11_XRes_INCLUDE_PATH
    X11_Xxf86misc_LIB
    X11_Xxf86misc_INCLUDE_PATH
    X11_Xxf86vm_LIB
    X11_Xxf86vm_INCLUDE_PATH
    X11_Xi_LIB
    X11_Xi_INCLUDE_PATH
    X11_Xinerama_LIB
    X11_Xinerama_INCLUDE_PATH
    X11_Xtst_LIB
    X11_Xtst_INCLUDE_PATH
    X11_Xcursor_LIB
    X11_Xcursor_INCLUDE_PATH
    X11_dpms_INCLUDE_PATH
    X11_Xt_LIB
    X11_Xt_INCLUDE_PATH
    X11_Xdmcp_LIB
    X11_LIBRARIES
    X11_Xaccessrules_INCLUDE_PATH
    X11_Xaccessstr_INCLUDE_PATH
    X11_Xdmcp_INCLUDE_PATH
    X11_Xkb_INCLUDE_PATH
    X11_Xkblib_INCLUDE_PATH
    X11_xkbcommon_INCLUDE_PATH
    X11_xkbcommon_LIB
    X11_xkbcommon_X11_INCLUDE_PATH
    X11_xkbcommon_X11_LIB
    X11_xkbfile_INCLUDE_PATH
    X11_xkbfile_LIB
    X11_Xmu_INCLUDE_PATH
    X11_Xmu_LIB
    X11_Xss_INCLUDE_PATH
    X11_Xss_LIB
    X11_Xpm_INCLUDE_PATH
    X11_Xpm_LIB
    X11_Xft_LIB
    X11_Xft_INCLUDE_PATH
    X11_Xshape_INCLUDE_PATH
    X11_Xv_LIB
    X11_Xv_INCLUDE_PATH
    X11_XShm_INCLUDE_PATH
    X11_ICE_LIB
    X11_ICE_INCLUDE_PATH
    X11_SM_LIB
    X11_SM_INCLUDE_PATH
    X11_XSync_INCLUDE_PATH
    X11_Xaw_LIB
    X11_Xaw_INCLUDE_PATH
  )
  set(CMAKE_FIND_FRAMEWORK ${CMAKE_FIND_FRAMEWORK_SAVE})
  set(CMAKE_REQUIRED_QUIET ${CMAKE_REQUIRED_QUIET_SAVE})
endif ()
