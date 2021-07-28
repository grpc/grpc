# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindOpenGL
----------

FindModule for OpenGL and OpenGL Utility Library (GLU).

Optional COMPONENTS
^^^^^^^^^^^^^^^^^^^

This module respects several optional COMPONENTS: ``EGL``, ``GLX``, and
``OpenGL``.  There are corresponding import targets for each of these flags.

IMPORTED Targets
^^^^^^^^^^^^^^^^

This module defines the :prop_tgt:`IMPORTED` targets:

``OpenGL::GL``
 Defined to the platform-specific OpenGL libraries if the system has OpenGL.
``OpenGL::OpenGL``
 Defined to libOpenGL if the system is GLVND-based.
``OpenGL::GLU``
 Defined if the system has OpenGL Utility Library (GLU).
``OpenGL::GLX``
 Defined if the system has OpenGL Extension to the X Window System (GLX).
``OpenGL::EGL``
 Defined if the system has EGL.

Result Variables
^^^^^^^^^^^^^^^^

This module sets the following variables:

``OPENGL_FOUND``
 True, if the system has OpenGL and all components are found.
``OPENGL_XMESA_FOUND``
 True, if the system has XMESA.
``OPENGL_GLU_FOUND``
 True, if the system has GLU.
``OpenGL_OpenGL_FOUND``
 True, if the system has an OpenGL library.
``OpenGL_GLX_FOUND``
 True, if the system has GLX.
``OpenGL_EGL_FOUND``
 True, if the system has EGL.
``OPENGL_INCLUDE_DIR``
 Path to the OpenGL include directory.
``OPENGL_EGL_INCLUDE_DIRS``
 Path to the EGL include directory.
``OPENGL_LIBRARIES``
 Paths to the OpenGL library, windowing system libraries, and GLU libraries.
 On Linux, this assumes GLX and is never correct for EGL-based targets.
 Clients are encouraged to use the ``OpenGL::*`` import targets instead.

Cache variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``OPENGL_egl_LIBRARY``
 Path to the EGL library.
``OPENGL_glu_LIBRARY``
 Path to the GLU library.
``OPENGL_glx_LIBRARY``
 Path to the GLVND 'GLX' library.
``OPENGL_opengl_LIBRARY``
 Path to the GLVND 'OpenGL' library
``OPENGL_gl_LIBRARY``
 Path to the OpenGL library.  New code should prefer the ``OpenGL::*`` import
 targets.

Linux-specific
^^^^^^^^^^^^^^

Some Linux systems utilize GLVND as a new ABI for OpenGL.  GLVND separates
context libraries from OpenGL itself; OpenGL lives in "libOpenGL", and
contexts are defined in "libGLX" or "libEGL".  GLVND is currently the only way
to get OpenGL 3+ functionality via EGL in a manner portable across vendors.
Projects may use GLVND explicitly with target ``OpenGL::OpenGL`` and either
``OpenGL::GLX`` or ``OpenGL::EGL``.

Projects may use the ``OpenGL::GL`` target (or ``OPENGL_LIBRARIES`` variable)
to use legacy GL interfaces.  These will use the legacy GL library located
by ``OPENGL_gl_LIBRARY``, if available.  If ``OPENGL_gl_LIBRARY`` is empty or
not found and GLVND is available, the ``OpenGL::GL`` target will use GLVND
``OpenGL::OpenGL`` and ``OpenGL::GLX`` (and the ``OPENGL_LIBRARIES``
variable will use the corresponding libraries).  Thus, for non-EGL-based
Linux targets, the ``OpenGL::GL`` target is most portable.

A ``OpenGL_GL_PREFERENCE`` variable may be set to specify the preferred way
to provide legacy GL interfaces in case multiple choices are available.
The value may be one of:

``GLVND``
 If the GLVND OpenGL and GLX libraries are available, prefer them.
 This forces ``OPENGL_gl_LIBRARY`` to be empty.
 This is the default if components were requested (since components
 correspond to GLVND libraries) or if policy :policy:`CMP0072` is
 set to ``NEW``.

``LEGACY``
 Prefer to use the legacy libGL library, if available.
 This is the default if no components were requested and
 policy :policy:`CMP0072` is not set to ``NEW``.

For EGL targets the client must rely on GLVND support on the user's system.
Linking should use the ``OpenGL::OpenGL OpenGL::EGL`` targets.  Using GLES*
libraries is theoretically possible in place of ``OpenGL::OpenGL``, but this
module does not currently support that; contributions welcome.

``OPENGL_egl_LIBRARY`` and ``OPENGL_EGL_INCLUDE_DIRS`` are defined in the case of
GLVND.  For non-GLVND Linux and other systems these are left undefined.

macOS-Specific
^^^^^^^^^^^^^^

On OSX FindOpenGL defaults to using the framework version of OpenGL. People
will have to change the cache values of OPENGL_glu_LIBRARY and
OPENGL_gl_LIBRARY to use OpenGL with X11 on OSX.
#]=======================================================================]

set(_OpenGL_REQUIRED_VARS OPENGL_gl_LIBRARY)

# Provide OPENGL_USE_<C> variables for each component.
foreach(component ${OpenGL_FIND_COMPONENTS})
  string(TOUPPER ${component} _COMPONENT)
  set(OPENGL_USE_${_COMPONENT} 1)
endforeach()

set(_OpenGL_CACHE_VARS)

if (WIN32)

  if(BORLAND)
    set (OPENGL_gl_LIBRARY import32 CACHE STRING "OpenGL library for win32")
    set (OPENGL_glu_LIBRARY import32 CACHE STRING "GLU library for win32")
  else()
    set (OPENGL_gl_LIBRARY opengl32 CACHE STRING "OpenGL library for win32")
    set (OPENGL_glu_LIBRARY glu32 CACHE STRING "GLU library for win32")
  endif()

  list(APPEND _OpenGL_CACHE_VARS
    OPENGL_gl_LIBRARY
    OPENGL_glu_LIBRARY
    )
elseif (APPLE)
  # The OpenGL.framework provides both gl and glu
  find_library(OPENGL_gl_LIBRARY OpenGL DOC "OpenGL library for OS X")
  find_library(OPENGL_glu_LIBRARY OpenGL DOC
    "GLU library for OS X (usually same as OpenGL library)")
  find_path(OPENGL_INCLUDE_DIR OpenGL/gl.h DOC "Include for OpenGL on OS X")
  list(APPEND _OpenGL_REQUIRED_VARS OPENGL_INCLUDE_DIR)

  list(APPEND _OpenGL_CACHE_VARS
    OPENGL_INCLUDE_DIR
    OPENGL_gl_LIBRARY
    OPENGL_glu_LIBRARY
    )
else()
  if (CMAKE_SYSTEM_NAME MATCHES "HP-UX")
    # Handle HP-UX cases where we only want to find OpenGL in either hpux64
    # or hpux32 depending on if we're doing a 64 bit build.
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
      set(_OPENGL_LIB_PATH
        /opt/graphics/OpenGL/lib/hpux32/)
    else()
      set(_OPENGL_LIB_PATH
        /opt/graphics/OpenGL/lib/hpux64/
        /opt/graphics/OpenGL/lib/pa20_64)
    endif()
  elseif(CMAKE_SYSTEM_NAME STREQUAL Haiku)
    set(_OPENGL_LIB_PATH
      /boot/develop/lib/x86)
    set(_OPENGL_INCLUDE_PATH
      /boot/develop/headers/os/opengl)
  endif()

  # The first line below is to make sure that the proper headers
  # are used on a Linux machine with the NVidia drivers installed.
  # They replace Mesa with NVidia's own library but normally do not
  # install headers and that causes the linking to
  # fail since the compiler finds the Mesa headers but NVidia's library.
  # Make sure the NVIDIA directory comes BEFORE the others.
  #  - Atanas Georgiev <atanas@cs.columbia.edu>
  find_path(OPENGL_INCLUDE_DIR GL/gl.h
    /usr/share/doc/NVIDIA_GLX-1.0/include
    /usr/openwin/share/include
    /opt/graphics/OpenGL/include
    ${_OPENGL_INCLUDE_PATH}
  )
  find_path(OPENGL_GLX_INCLUDE_DIR GL/glx.h ${_OPENGL_INCLUDE_PATH})
  find_path(OPENGL_EGL_INCLUDE_DIR EGL/egl.h ${_OPENGL_INCLUDE_PATH})
  find_path(OPENGL_xmesa_INCLUDE_DIR GL/xmesa.h
    /usr/share/doc/NVIDIA_GLX-1.0/include
    /usr/openwin/share/include
    /opt/graphics/OpenGL/include
  )
  list(APPEND _OpenGL_CACHE_VARS
    OPENGL_INCLUDE_DIR
    OPENGL_GLX_INCLUDE_DIR
    OPENGL_EGL_INCLUDE_DIR
    OPENGL_xmesa_INCLUDE_DIR
    )

  # Search for the GLVND libraries.  We do this regardless of COMPONENTS; we'll
  # take into account the COMPONENTS logic later.
  find_library(OPENGL_opengl_LIBRARY
    NAMES OpenGL
    PATHS ${_OPENGL_LIB_PATH}
  )

  find_library(OPENGL_glx_LIBRARY
    NAMES GLX
    PATHS ${_OPENGL_LIB_PATH}
    PATH_SUFFIXES libglvnd
  )

  find_library(OPENGL_egl_LIBRARY
    NAMES EGL
    PATHS ${_OPENGL_LIB_PATH}
    PATH_SUFFIXES libglvnd
  )

  find_library(OPENGL_glu_LIBRARY
    NAMES GLU MesaGLU
    PATHS ${OPENGL_gl_LIBRARY}
          /opt/graphics/OpenGL/lib
          /usr/openwin/lib
          /usr/shlib
  )

  list(APPEND _OpenGL_CACHE_VARS
    OPENGL_opengl_LIBRARY
    OPENGL_glx_LIBRARY
    OPENGL_egl_LIBRARY
    OPENGL_glu_LIBRARY
    )

  set(_OpenGL_GL_POLICY_WARN 0)
  if(NOT DEFINED OpenGL_GL_PREFERENCE)
    set(OpenGL_GL_PREFERENCE "")
  endif()
  if(NOT OpenGL_GL_PREFERENCE STREQUAL "")
    # A preference has been explicitly specified.
    if(NOT OpenGL_GL_PREFERENCE MATCHES "^(GLVND|LEGACY)$")
      message(FATAL_ERROR
        "OpenGL_GL_PREFERENCE value '${OpenGL_GL_PREFERENCE}' not recognized.  "
        "Allowed values are 'GLVND' and 'LEGACY'."
        )
    endif()
  elseif(OpenGL_FIND_COMPONENTS)
    # No preference was explicitly specified, but the caller did request
    # at least one GLVND component.  Prefer GLVND for legacy GL.
    set(OpenGL_GL_PREFERENCE "GLVND")
  else()
    # No preference was explicitly specified and no GLVND components were
    # requested.  Use a policy to choose the default.
    cmake_policy(GET CMP0072 _OpenGL_GL_POLICY)
    if("x${_OpenGL_GL_POLICY}x" STREQUAL "xNEWx")
      set(OpenGL_GL_PREFERENCE "GLVND")
    else()
      set(OpenGL_GL_PREFERENCE "LEGACY")
      if("x${_OpenGL_GL_POLICY}x" STREQUAL "xx")
        set(_OpenGL_GL_POLICY_WARN 1)
      endif()
    endif()
    unset(_OpenGL_GL_POLICY)
  endif()

  if("x${OpenGL_GL_PREFERENCE}x" STREQUAL "xGLVNDx" AND OPENGL_opengl_LIBRARY AND OPENGL_glx_LIBRARY)
    # We can provide legacy GL using GLVND libraries.
    # Do not use any legacy GL library.
    set(OPENGL_gl_LIBRARY "")
  else()
    # We cannot provide legacy GL using GLVND libraries.
    # Search for the legacy GL library.
    find_library(OPENGL_gl_LIBRARY
      NAMES GL MesaGL
      PATHS /opt/graphics/OpenGL/lib
            /usr/openwin/lib
            /usr/shlib
            ${_OPENGL_LIB_PATH}
      PATH_SUFFIXES libglvnd
      )
    list(APPEND _OpenGL_CACHE_VARS OPENGL_gl_LIBRARY)
  endif()

  if(_OpenGL_GL_POLICY_WARN AND OPENGL_gl_LIBRARY AND OPENGL_opengl_LIBRARY AND OPENGL_glx_LIBRARY)
    cmake_policy(GET_WARNING CMP0072 _cmp0072_warning)
    message(AUTHOR_WARNING
      "${_cmp0072_warning}\n"
      "FindOpenGL found both a legacy GL library:\n"
      "  OPENGL_gl_LIBRARY: ${OPENGL_gl_LIBRARY}\n"
      "and GLVND libraries for OpenGL and GLX:\n"
      "  OPENGL_opengl_LIBRARY: ${OPENGL_opengl_LIBRARY}\n"
      "  OPENGL_glx_LIBRARY: ${OPENGL_glx_LIBRARY}\n"
      "OpenGL_GL_PREFERENCE has not been set to \"GLVND\" or \"LEGACY\", so for "
      "compatibility with CMake 3.10 and below the legacy GL library will be used."
      )
  endif()
  unset(_OpenGL_GL_POLICY_WARN)

  # FPHSA cannot handle "this OR that is required", so we conditionally set what
  # it must look for.  First clear any previous config we might have done:
  set(_OpenGL_REQUIRED_VARS)

  # now we append the libraries as appropriate.  The complicated logic
  # basically comes down to "use libOpenGL when we can, and add in specific
  # context mechanisms when requested, or we need them to preserve the previous
  # default where glx is always available."
  if((NOT OPENGL_USE_EGL AND
      NOT OPENGL_opengl_LIBRARY AND
          OPENGL_glx_LIBRARY AND
      NOT OPENGL_gl_LIBRARY) OR
     (NOT OPENGL_USE_EGL AND
      NOT OPENGL_glx_LIBRARY AND
      NOT OPENGL_gl_LIBRARY) OR
     (NOT OPENGL_USE_EGL AND
          OPENGL_opengl_LIBRARY AND
          OPENGL_glx_LIBRARY) OR
     (    OPENGL_USE_EGL))
    list(APPEND _OpenGL_REQUIRED_VARS OPENGL_opengl_LIBRARY)
  endif()

  # GLVND GLX library.  Preferred when available.
  if((NOT OPENGL_USE_OPENGL AND
      NOT OPENGL_USE_GLX AND
      NOT OPENGL_USE_EGL AND
      NOT OPENGL_glx_LIBRARY AND
      NOT OPENGL_gl_LIBRARY) OR
     (    OPENGL_USE_GLX AND
      NOT OPENGL_USE_EGL AND
      NOT OPENGL_glx_LIBRARY AND
      NOT OPENGL_gl_LIBRARY) OR
     (NOT OPENGL_USE_EGL AND
          OPENGL_opengl_LIBRARY AND
          OPENGL_glx_LIBRARY) OR
     (OPENGL_USE_GLX AND OPENGL_USE_EGL))
    list(APPEND _OpenGL_REQUIRED_VARS OPENGL_glx_LIBRARY)
  endif()

  # GLVND EGL library.
  if(OPENGL_USE_EGL)
    list(APPEND _OpenGL_REQUIRED_VARS OPENGL_egl_LIBRARY)
  endif()

  # Old-style "libGL" library: used as a fallback when GLVND isn't available.
  if((NOT OPENGL_USE_EGL AND
      NOT OPENGL_opengl_LIBRARY AND
          OPENGL_glx_LIBRARY AND
          OPENGL_gl_LIBRARY) OR
     (NOT OPENGL_USE_EGL AND
      NOT OPENGL_glx_LIBRARY AND
          OPENGL_gl_LIBRARY))
    list(APPEND _OpenGL_REQUIRED_VARS OPENGL_gl_LIBRARY)
  endif()

  # We always need the 'gl.h' include dir.
  list(APPEND _OpenGL_REQUIRED_VARS OPENGL_INCLUDE_DIR)

  unset(_OPENGL_INCLUDE_PATH)
  unset(_OPENGL_LIB_PATH)

  find_library(OPENGL_glu_LIBRARY
    NAMES GLU MesaGLU
    PATHS ${OPENGL_gl_LIBRARY}
          /opt/graphics/OpenGL/lib
          /usr/openwin/lib
          /usr/shlib
  )
endif ()

if(OPENGL_xmesa_INCLUDE_DIR)
  set( OPENGL_XMESA_FOUND "YES" )
else()
  set( OPENGL_XMESA_FOUND "NO" )
endif()

if(OPENGL_glu_LIBRARY)
  set( OPENGL_GLU_FOUND "YES" )
else()
  set( OPENGL_GLU_FOUND "NO" )
endif()

# OpenGL_OpenGL_FOUND is a bit unique in that it is okay if /either/ libOpenGL
# or libGL is found.
# Using libGL with libEGL is never okay, though; we handle that case later.
if(NOT OPENGL_opengl_LIBRARY AND NOT OPENGL_gl_LIBRARY)
  set(OpenGL_OpenGL_FOUND FALSE)
else()
  set(OpenGL_OpenGL_FOUND TRUE)
endif()

if(OPENGL_glx_LIBRARY AND OPENGL_GLX_INCLUDE_DIR)
  set(OpenGL_GLX_FOUND TRUE)
else()
  set(OpenGL_GLX_FOUND FALSE)
endif()

if(OPENGL_egl_LIBRARY AND OPENGL_EGL_INCLUDE_DIR)
  set(OpenGL_EGL_FOUND TRUE)
else()
  set(OpenGL_EGL_FOUND FALSE)
endif()

# User-visible names should be plural.
if(OPENGL_EGL_INCLUDE_DIR)
  set(OPENGL_EGL_INCLUDE_DIRS ${OPENGL_EGL_INCLUDE_DIR})
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
if (CMAKE_FIND_PACKAGE_NAME STREQUAL "GLU")
  # FindGLU include()'s this module. It's an old pattern, but rather than
  # trying to suppress this from outside the module (which is then sensitive to
  # the contents, detect the case in this module and suppress it explicitly.
  set(FPHSA_NAME_MISMATCHED 1)
endif ()
FIND_PACKAGE_HANDLE_STANDARD_ARGS(OpenGL REQUIRED_VARS ${_OpenGL_REQUIRED_VARS}
                                  HANDLE_COMPONENTS)
unset(FPHSA_NAME_MISMATCHED)
unset(_OpenGL_REQUIRED_VARS)

# OpenGL:: targets
if(OPENGL_FOUND)
  # ::OpenGL is a GLVND library, and thus Linux-only: we don't bother checking
  # for a framework version of this library.
  if(OPENGL_opengl_LIBRARY AND NOT TARGET OpenGL::OpenGL)
    if(IS_ABSOLUTE "${OPENGL_opengl_LIBRARY}")
      add_library(OpenGL::OpenGL UNKNOWN IMPORTED)
      set_target_properties(OpenGL::OpenGL PROPERTIES IMPORTED_LOCATION
                            "${OPENGL_opengl_LIBRARY}")
    else()
      add_library(OpenGL::OpenGL INTERFACE IMPORTED)
      set_target_properties(OpenGL::OpenGL PROPERTIES IMPORTED_LIBNAME
                            "${OPENGL_opengl_LIBRARY}")
    endif()
    set_target_properties(OpenGL::OpenGL PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                          "${OPENGL_INCLUDE_DIR}")
  endif()

  # ::GLX is a GLVND library, and thus Linux-only: we don't bother checking
  # for a framework version of this library.
  if(OpenGL_GLX_FOUND AND NOT TARGET OpenGL::GLX)
    if(IS_ABSOLUTE "${OPENGL_glx_LIBRARY}")
      add_library(OpenGL::GLX UNKNOWN IMPORTED)
      set_target_properties(OpenGL::GLX PROPERTIES IMPORTED_LOCATION
                            "${OPENGL_glx_LIBRARY}")
    else()
      add_library(OpenGL::GLX INTERFACE IMPORTED)
      set_target_properties(OpenGL::GLX PROPERTIES IMPORTED_LIBNAME
                            "${OPENGL_glx_LIBRARY}")
    endif()
    set_target_properties(OpenGL::GLX PROPERTIES INTERFACE_LINK_LIBRARIES
                          OpenGL::OpenGL)
    set_target_properties(OpenGL::GLX PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                          "${OPENGL_GLX_INCLUDE_DIR}")
  endif()

  if(OPENGL_gl_LIBRARY AND NOT TARGET OpenGL::GL)
    # A legacy GL library is available, so use it for the legacy GL target.
    if(IS_ABSOLUTE "${OPENGL_gl_LIBRARY}")
      add_library(OpenGL::GL UNKNOWN IMPORTED)
      if(OPENGL_gl_LIBRARY MATCHES "/([^/]+)\\.framework$")
        set(_gl_fw "${OPENGL_gl_LIBRARY}/${CMAKE_MATCH_1}")
        if(EXISTS "${_gl_fw}.tbd")
          string(APPEND _gl_fw ".tbd")
        endif()
        set_target_properties(OpenGL::GL PROPERTIES
          IMPORTED_LOCATION "${_gl_fw}")
      else()
        set_target_properties(OpenGL::GL PROPERTIES
          IMPORTED_LOCATION "${OPENGL_gl_LIBRARY}")
      endif()
    else()
      add_library(OpenGL::GL INTERFACE IMPORTED)
      set_target_properties(OpenGL::GL PROPERTIES
        IMPORTED_LIBNAME "${OPENGL_gl_LIBRARY}")
    endif()
    set_target_properties(OpenGL::GL PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${OPENGL_INCLUDE_DIR}")
  elseif(NOT TARGET OpenGL::GL AND TARGET OpenGL::OpenGL AND TARGET OpenGL::GLX)
    # A legacy GL library is not available, but we can provide the legacy GL
    # target using GLVND OpenGL+GLX.
    add_library(OpenGL::GL INTERFACE IMPORTED)
    set_target_properties(OpenGL::GL PROPERTIES INTERFACE_LINK_LIBRARIES
                          OpenGL::OpenGL)
    set_property(TARGET OpenGL::GL APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                 OpenGL::GLX)
    set_target_properties(OpenGL::GL PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                          "${OPENGL_INCLUDE_DIR}")
  endif()

  # ::EGL is a GLVND library, and thus Linux-only: we don't bother checking
  # for a framework version of this library.
  # Note we test for OpenGL::OpenGL as a target.  When this module is updated to
  # support GLES, we would additionally want to check for the hypothetical GLES
  # target and enable EGL if either ::GLES or ::OpenGL is created.
  if(TARGET OpenGL::OpenGL AND OpenGL_EGL_FOUND AND NOT TARGET OpenGL::EGL)
    if(IS_ABSOLUTE "${OPENGL_egl_LIBRARY}")
      add_library(OpenGL::EGL UNKNOWN IMPORTED)
      set_target_properties(OpenGL::EGL PROPERTIES IMPORTED_LOCATION
                            "${OPENGL_egl_LIBRARY}")
    else()
      add_library(OpenGL::EGL INTERFACE IMPORTED)
      set_target_properties(OpenGL::EGL PROPERTIES IMPORTED_LIBNAME
                            "${OPENGL_egl_LIBRARY}")
    endif()
    set_target_properties(OpenGL::EGL PROPERTIES INTERFACE_LINK_LIBRARIES
                          OpenGL::OpenGL)
    # Note that EGL's include directory is different from OpenGL/GLX's!
    set_target_properties(OpenGL::EGL PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
                          "${OPENGL_EGL_INCLUDE_DIR}")
  endif()

  if(OPENGL_GLU_FOUND AND NOT TARGET OpenGL::GLU)
    if(IS_ABSOLUTE "${OPENGL_glu_LIBRARY}")
      add_library(OpenGL::GLU UNKNOWN IMPORTED)
      if(OPENGL_glu_LIBRARY MATCHES "/([^/]+)\\.framework$")
        set(_glu_fw "${OPENGL_glu_LIBRARY}/${CMAKE_MATCH_1}")
        if(EXISTS "${_glu_fw}.tbd")
          string(APPEND _glu_fw ".tbd")
        endif()
        set_target_properties(OpenGL::GLU PROPERTIES
          IMPORTED_LOCATION "${_glu_fw}")
      else()
        set_target_properties(OpenGL::GLU PROPERTIES
          IMPORTED_LOCATION "${OPENGL_glu_LIBRARY}")
      endif()
    else()
      add_library(OpenGL::GLU INTERFACE IMPORTED)
      set_target_properties(OpenGL::GLU PROPERTIES
        IMPORTED_LIBNAME "${OPENGL_glu_LIBRARY}")
    endif()
    set_target_properties(OpenGL::GLU PROPERTIES
      INTERFACE_LINK_LIBRARIES OpenGL::GL)
  endif()

  # OPENGL_LIBRARIES mirrors OpenGL::GL's logic ...
  if(OPENGL_gl_LIBRARY)
    set(OPENGL_LIBRARIES ${OPENGL_gl_LIBRARY})
  elseif(TARGET OpenGL::OpenGL AND TARGET OpenGL::GLX)
    set(OPENGL_LIBRARIES ${OPENGL_opengl_LIBRARY} ${OPENGL_glx_LIBRARY})
  else()
    set(OPENGL_LIBRARIES "")
  endif()
  # ... and also includes GLU, if available.
  if(TARGET OpenGL::GLU)
    list(APPEND OPENGL_LIBRARIES ${OPENGL_glu_LIBRARY})
  endif()
endif()

# This deprecated setting is for backward compatibility with CMake1.4
set(OPENGL_LIBRARY ${OPENGL_LIBRARIES})
# This deprecated setting is for backward compatibility with CMake1.4
set(OPENGL_INCLUDE_PATH ${OPENGL_INCLUDE_DIR})

mark_as_advanced(${_OpenGL_CACHE_VARS})
unset(_OpenGL_CACHE_VARS)
