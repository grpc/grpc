# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindImageMagick
---------------

Find ImageMagick binary suite.

This module will search for a set of ImageMagick tools specified as
components in the :command:`find_package` call.  Typical components include,
but are not limited to (future versions of ImageMagick might have
additional components not listed here):

::

  animate
  compare
  composite
  conjure
  convert
  display
  identify
  import
  mogrify
  montage
  stream



If no component is specified in the :command:`find_package` call, then it only
searches for the ImageMagick executable directory.  This code defines
the following variables:

::

  ImageMagick_FOUND                  - TRUE if all components are found.
  ImageMagick_EXECUTABLE_DIR         - Full path to executables directory.
  ImageMagick_<component>_FOUND      - TRUE if <component> is found.
  ImageMagick_<component>_EXECUTABLE - Full path to <component> executable.
  ImageMagick_VERSION_STRING         - the version of ImageMagick found
                                       (since CMake 2.8.8)



``ImageMagick_VERSION_STRING`` will not work for old versions like 5.2.3.

There are also components for the following ImageMagick APIs:

::

  Magick++
  MagickWand
  MagickCore



For these components the following variables are set:

::

  ImageMagick_FOUND                    - TRUE if all components are found.
  ImageMagick_INCLUDE_DIRS             - Full paths to all include dirs.
  ImageMagick_LIBRARIES                - Full paths to all libraries.
  ImageMagick_<component>_FOUND        - TRUE if <component> is found.
  ImageMagick_<component>_INCLUDE_DIRS - Full path to <component> include dirs.
  ImageMagick_<component>_LIBRARIES    - Full path to <component> libraries.



Example Usages:

::

  find_package(ImageMagick)
  find_package(ImageMagick COMPONENTS convert)
  find_package(ImageMagick COMPONENTS convert mogrify display)
  find_package(ImageMagick COMPONENTS Magick++)
  find_package(ImageMagick COMPONENTS Magick++ convert)



Note that the standard :command:`find_package` features are supported (i.e.,
``QUIET``, ``REQUIRED``, etc.).
#]=======================================================================]

find_package(PkgConfig QUIET)

#---------------------------------------------------------------------
# Helper functions
#---------------------------------------------------------------------
function(FIND_IMAGEMAGICK_API component header)
  set(ImageMagick_${component}_FOUND FALSE PARENT_SCOPE)

  pkg_check_modules(PC_${component} QUIET ${component})

  find_path(ImageMagick_${component}_INCLUDE_DIR
    NAMES ${header}
    HINTS
      ${PC_${component}_INCLUDEDIR}
      ${PC_${component}_INCLUDE_DIRS}
    PATHS
      ${ImageMagick_INCLUDE_DIRS}
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\ImageMagick\\Current;BinPath]/include"
    PATH_SUFFIXES
      ImageMagick ImageMagick-6 ImageMagick-7
    DOC "Path to the ImageMagick arch-independent include dir."
    NO_DEFAULT_PATH
    )
  find_path(ImageMagick_${component}_ARCH_INCLUDE_DIR
    NAMES magick/magick-baseconfig.h
    HINTS
      ${PC_${component}_INCLUDEDIR}
      ${PC_${component}_INCLUDE_DIRS}
    PATHS
      ${ImageMagick_INCLUDE_DIRS}
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\ImageMagick\\Current;BinPath]/include"
    PATH_SUFFIXES
      ImageMagick ImageMagick-6 ImageMagick-7
    DOC "Path to the ImageMagick arch-specific include dir."
    NO_DEFAULT_PATH
    )
  find_library(ImageMagick_${component}_LIBRARY
    NAMES ${ARGN}
    HINTS
      ${PC_${component}_LIBDIR}
      ${PC_${component}_LIB_DIRS}
    PATHS
      "[HKEY_LOCAL_MACHINE\\SOFTWARE\\ImageMagick\\Current;BinPath]/lib"
    DOC "Path to the ImageMagick Magick++ library."
    NO_DEFAULT_PATH
    )

  # old version have only indep dir
  if(ImageMagick_${component}_INCLUDE_DIR AND ImageMagick_${component}_LIBRARY)
    set(ImageMagick_${component}_FOUND TRUE PARENT_SCOPE)

    # Construct per-component include directories.
    set(ImageMagick_${component}_INCLUDE_DIRS
      ${ImageMagick_${component}_INCLUDE_DIR}
      )
    if(ImageMagick_${component}_ARCH_INCLUDE_DIR)
      list(APPEND ImageMagick_${component}_INCLUDE_DIRS
        ${ImageMagick_${component}_ARCH_INCLUDE_DIR})
    endif()
    list(REMOVE_DUPLICATES ImageMagick_${component}_INCLUDE_DIRS)
    set(ImageMagick_${component}_INCLUDE_DIRS
      ${ImageMagick_${component}_INCLUDE_DIRS} PARENT_SCOPE)

    # Add the per-component include directories to the full include dirs.
    list(APPEND ImageMagick_INCLUDE_DIRS ${ImageMagick_${component}_INCLUDE_DIRS})
    list(REMOVE_DUPLICATES ImageMagick_INCLUDE_DIRS)
    set(ImageMagick_INCLUDE_DIRS ${ImageMagick_INCLUDE_DIRS} PARENT_SCOPE)

    list(APPEND ImageMagick_LIBRARIES
      ${ImageMagick_${component}_LIBRARY}
      )
    set(ImageMagick_LIBRARIES ${ImageMagick_LIBRARIES} PARENT_SCOPE)
  endif()
endfunction()

function(FIND_IMAGEMAGICK_EXE component)
  set(_IMAGEMAGICK_EXECUTABLE
    ${ImageMagick_EXECUTABLE_DIR}/${component}${CMAKE_EXECUTABLE_SUFFIX})
  if(EXISTS ${_IMAGEMAGICK_EXECUTABLE})
    set(ImageMagick_${component}_EXECUTABLE
      ${_IMAGEMAGICK_EXECUTABLE}
       PARENT_SCOPE
       )
    set(ImageMagick_${component}_FOUND TRUE PARENT_SCOPE)
  else()
    set(ImageMagick_${component}_FOUND FALSE PARENT_SCOPE)
  endif()
endfunction()

#---------------------------------------------------------------------
# Start Actual Work
#---------------------------------------------------------------------
# Try to find a ImageMagick installation binary path.
find_path(ImageMagick_EXECUTABLE_DIR
  NAMES mogrify${CMAKE_EXECUTABLE_SUFFIX}
  PATHS
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\ImageMagick\\Current;BinPath]"
  DOC "Path to the ImageMagick binary directory."
  NO_DEFAULT_PATH
  )
find_path(ImageMagick_EXECUTABLE_DIR
  NAMES mogrify${CMAKE_EXECUTABLE_SUFFIX}
  )

# Find each component. Search for all tools in same dir
# <ImageMagick_EXECUTABLE_DIR>; otherwise they should be found
# independently and not in a cohesive module such as this one.
unset(ImageMagick_REQUIRED_VARS)
unset(ImageMagick_DEFAULT_EXECUTABLES)
foreach(component ${ImageMagick_FIND_COMPONENTS}
    # DEPRECATED: forced components for backward compatibility
    convert mogrify import montage composite
    )
  if(component STREQUAL "Magick++")
    FIND_IMAGEMAGICK_API(Magick++ Magick++.h
      Magick++ CORE_RL_Magick++_
      Magick++-6 Magick++-7
      Magick++-Q8 Magick++-Q16 Magick++-Q16HDRI Magick++-Q8HDRI
      Magick++-6.Q64 Magick++-6.Q32 Magick++-6.Q64HDRI Magick++-6.Q32HDRI
      Magick++-6.Q16 Magick++-6.Q8 Magick++-6.Q16HDRI Magick++-6.Q8HDRI
      Magick++-7.Q64 Magick++-7.Q32 Magick++-7.Q64HDRI Magick++-7.Q32HDRI
      Magick++-7.Q16 Magick++-7.Q8 Magick++-7.Q16HDRI Magick++-7.Q8HDRI
      )
    list(APPEND ImageMagick_REQUIRED_VARS ImageMagick_Magick++_LIBRARY)
  elseif(component STREQUAL "MagickWand")
    FIND_IMAGEMAGICK_API(MagickWand "wand/MagickWand.h;MagickWand/MagickWand.h"
      Wand MagickWand CORE_RL_wand_ CORE_RL_MagickWand_
      MagickWand-6 MagickWand-7
      MagickWand-Q16 MagickWand-Q8 MagickWand-Q16HDRI MagickWand-Q8HDRI
      MagickWand-6.Q64 MagickWand-6.Q32 MagickWand-6.Q64HDRI MagickWand-6.Q32HDRI
      MagickWand-6.Q16 MagickWand-6.Q8 MagickWand-6.Q16HDRI MagickWand-6.Q8HDRI
      MagickWand-7.Q64 MagickWand-7.Q32 MagickWand-7.Q64HDRI MagickWand-7.Q32HDRI
      MagickWand-7.Q16 MagickWand-7.Q8 MagickWand-7.Q16HDRI MagickWand-7.Q8HDRI
      )
    list(APPEND ImageMagick_REQUIRED_VARS ImageMagick_MagickWand_LIBRARY)
  elseif(component STREQUAL "MagickCore")
    FIND_IMAGEMAGICK_API(MagickCore "magick/MagickCore.h;MagickCore/MagickCore.h"
      Magick MagickCore CORE_RL_magick_ CORE_RL_MagickCore_
      MagickCore-6 MagickCore-7
      MagickCore-Q16 MagickCore-Q8 MagickCore-Q16HDRI MagickCore-Q8HDRI
      MagickCore-6.Q64 MagickCore-6.Q32 MagickCore-6.Q64HDRI MagickCore-6.Q32HDRI
      MagickCore-6.Q16 MagickCore-6.Q8 MagickCore-6.Q16HDRI MagickCore-6.Q8HDRI
      MagickCore-7.Q64 MagickCore-7.Q32 MagickCore-7.Q64HDRI MagickCore-7.Q32HDRI
      MagickCore-7.Q16 MagickCore-7.Q8 MagickCore-7.Q16HDRI MagickCore-7.Q8HDRI
      )
    list(APPEND ImageMagick_REQUIRED_VARS ImageMagick_MagickCore_LIBRARY)
  else()
    if(ImageMagick_EXECUTABLE_DIR)
      FIND_IMAGEMAGICK_EXE(${component})
    endif()

    if(ImageMagick_FIND_COMPONENTS)
      list(FIND ImageMagick_FIND_COMPONENTS ${component} is_requested)
      if(is_requested GREATER -1)
        list(APPEND ImageMagick_REQUIRED_VARS ImageMagick_${component}_EXECUTABLE)
      endif()
    elseif(ImageMagick_${component}_EXECUTABLE)
      # if no components were requested explicitly put all (default) executables
      # in the list
      list(APPEND ImageMagick_DEFAULT_EXECUTABLES ImageMagick_${component}_EXECUTABLE)
    endif()
  endif()
endforeach()

if(NOT ImageMagick_FIND_COMPONENTS AND NOT ImageMagick_DEFAULT_EXECUTABLES)
  # No components were requested, and none of the default components were
  # found. Just insert mogrify into the list of the default components to
  # find so FPHSA below has something to check
  list(APPEND ImageMagick_REQUIRED_VARS ImageMagick_mogrify_EXECUTABLE)
elseif(ImageMagick_DEFAULT_EXECUTABLES)
  list(APPEND ImageMagick_REQUIRED_VARS ${ImageMagick_DEFAULT_EXECUTABLES})
endif()

set(ImageMagick_INCLUDE_DIRS ${ImageMagick_INCLUDE_DIRS})
set(ImageMagick_LIBRARIES ${ImageMagick_LIBRARIES})

if(ImageMagick_mogrify_EXECUTABLE)
  execute_process(COMMAND ${ImageMagick_mogrify_EXECUTABLE} -version
                  OUTPUT_VARIABLE imagemagick_version
                  ERROR_QUIET
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(imagemagick_version MATCHES "^Version: ImageMagick ([-0-9\\.]+)")
    set(ImageMagick_VERSION_STRING "${CMAKE_MATCH_1}")
  endif()
  unset(imagemagick_version)
endif()

#---------------------------------------------------------------------
# Standard Package Output
#---------------------------------------------------------------------
include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ImageMagick
                                  REQUIRED_VARS ${ImageMagick_REQUIRED_VARS}
                                  VERSION_VAR ImageMagick_VERSION_STRING
  )
# Maintain consistency with all other variables.
set(ImageMagick_FOUND ${IMAGEMAGICK_FOUND})

#---------------------------------------------------------------------
# DEPRECATED: Setting variables for backward compatibility.
#---------------------------------------------------------------------
set(IMAGEMAGICK_BINARY_PATH          ${ImageMagick_EXECUTABLE_DIR}
    CACHE PATH "Path to the ImageMagick binary directory.")
set(IMAGEMAGICK_CONVERT_EXECUTABLE   ${ImageMagick_convert_EXECUTABLE}
    CACHE FILEPATH "Path to ImageMagick's convert executable.")
set(IMAGEMAGICK_MOGRIFY_EXECUTABLE   ${ImageMagick_mogrify_EXECUTABLE}
    CACHE FILEPATH "Path to ImageMagick's mogrify executable.")
set(IMAGEMAGICK_IMPORT_EXECUTABLE    ${ImageMagick_import_EXECUTABLE}
    CACHE FILEPATH "Path to ImageMagick's import executable.")
set(IMAGEMAGICK_MONTAGE_EXECUTABLE   ${ImageMagick_montage_EXECUTABLE}
    CACHE FILEPATH "Path to ImageMagick's montage executable.")
set(IMAGEMAGICK_COMPOSITE_EXECUTABLE ${ImageMagick_composite_EXECUTABLE}
    CACHE FILEPATH "Path to ImageMagick's composite executable.")
mark_as_advanced(
  IMAGEMAGICK_BINARY_PATH
  IMAGEMAGICK_CONVERT_EXECUTABLE
  IMAGEMAGICK_MOGRIFY_EXECUTABLE
  IMAGEMAGICK_IMPORT_EXECUTABLE
  IMAGEMAGICK_MONTAGE_EXECUTABLE
  IMAGEMAGICK_COMPOSITE_EXECUTABLE
  )
