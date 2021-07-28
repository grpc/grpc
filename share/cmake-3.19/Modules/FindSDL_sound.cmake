# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindSDL_sound
-------------

Locates the SDL_sound library



This module depends on SDL being found and must be called AFTER
FindSDL.cmake is called.

This module defines

::

  SDL_SOUND_INCLUDE_DIR, where to find SDL_sound.h
  SDL_SOUND_FOUND, if false, do not try to link to SDL_sound
  SDL_SOUND_LIBRARIES, this contains the list of libraries that you need
    to link against.
  SDL_SOUND_EXTRAS, this is an optional variable for you to add your own
    flags to SDL_SOUND_LIBRARIES. This is prepended to SDL_SOUND_LIBRARIES.
    This is available mostly for cases this module failed to anticipate for
    and you must add additional flags. This is marked as ADVANCED.
  SDL_SOUND_VERSION_STRING, human-readable string containing the
    version of SDL_sound



This module also defines (but you shouldn't need to use directly)

::

   SDL_SOUND_LIBRARY, the name of just the SDL_sound library you would link
   against. Use SDL_SOUND_LIBRARIES for you link instructions and not this one.

And might define the following as needed

::

   MIKMOD_LIBRARY
   MODPLUG_LIBRARY
   OGG_LIBRARY
   VORBIS_LIBRARY
   SMPEG_LIBRARY
   FLAC_LIBRARY
   SPEEX_LIBRARY



Typically, you should not use these variables directly, and you should
use SDL_SOUND_LIBRARIES which contains SDL_SOUND_LIBRARY and the other
audio libraries (if needed) to successfully compile on your system.

Created by Eric Wing.  This module is a bit more complicated than the
other FindSDL* family modules.  The reason is that SDL_sound can be
compiled in a large variety of different ways which are independent of
platform.  SDL_sound may dynamically link against other 3rd party
libraries to get additional codec support, such as Ogg Vorbis, SMPEG,
ModPlug, MikMod, FLAC, Speex, and potentially others.  Under some
circumstances which I don't fully understand, there seems to be a
requirement that dependent libraries of libraries you use must also be
explicitly linked against in order to successfully compile.  SDL_sound
does not currently have any system in place to know how it was
compiled.  So this CMake module does the hard work in trying to
discover which 3rd party libraries are required for building (if any).
This module uses a brute force approach to create a test program that
uses SDL_sound, and then tries to build it.  If the build fails, it
parses the error output for known symbol names to figure out which
libraries are needed.

Responds to the $SDLDIR and $SDLSOUNDDIR environmental variable that
would correspond to the ./configure --prefix=$SDLDIR used in building
SDL.

On OSX, this will prefer the Framework version (if found) over others.
People will have to manually change the cache values of SDL_LIBRARY to
override this selectionor set the CMake environment CMAKE_INCLUDE_PATH
to modify the search paths.
#]=======================================================================]

set(SDL_SOUND_EXTRAS "" CACHE STRING "SDL_sound extra flags")
mark_as_advanced(SDL_SOUND_EXTRAS)

# Find SDL_sound.h
find_path(SDL_SOUND_INCLUDE_DIR SDL_sound.h
  HINTS
    ENV SDLSOUNDDIR
    ENV SDLDIR
  PATH_SUFFIXES SDL
                # path suffixes to search inside ENV{SDLDIR}
                include/SDL include/SDL12 include/SDL11 include
  )

find_library(SDL_SOUND_LIBRARY
  NAMES SDL_sound
  HINTS
    ENV SDLSOUNDDIR
    ENV SDLDIR
  PATH_SUFFIXES lib VisualC/win32lib
  )

if(SDL_FOUND AND SDL_SOUND_INCLUDE_DIR AND SDL_SOUND_LIBRARY)

  # CMake is giving me problems using TRY_COMPILE with the CMAKE_FLAGS
  # for the :STRING syntax if I have multiple values contained in a
  # single variable. This is a problem for the SDL_LIBRARY variable
  # because it does just that. When I feed this variable to the command,
  # only the first value gets the appropriate modifier (e.g. -I) and
  # the rest get dropped.
  # To get multiple single variables to work, I must separate them with a "\;"
  # I could go back and modify the FindSDL.cmake module, but that's kind of painful.
  # The solution would be to try something like:
  # string(APPEND SDL_TRY_COMPILE_LIBRARY_LIST "\;${CMAKE_THREAD_LIBS_INIT}")
  # Instead, it was suggested on the mailing list to write a temporary CMakeLists.txt
  # with a temporary test project and invoke that with TRY_COMPILE.
  # See message thread "Figuring out dependencies for a library in order to build"
  # 2005-07-16
  #     try_compile(
  #             MY_RESULT
  #             ${CMAKE_BINARY_DIR}
  #             ${PROJECT_SOURCE_DIR}/DetermineSoundLibs.c
  #             CMAKE_FLAGS
  #                     -DINCLUDE_DIRECTORIES:STRING=${SDL_INCLUDE_DIR}\;${SDL_SOUND_INCLUDE_DIR}
  #                     -DLINK_LIBRARIES:STRING=${SDL_SOUND_LIBRARY}\;${SDL_LIBRARY}
  #             OUTPUT_VARIABLE MY_OUTPUT
  #     )

  # To minimize external dependencies, create a sdlsound test program
  # which will be used to figure out if additional link dependencies are
  # required for the link phase.
  file(WRITE ${PROJECT_BINARY_DIR}/CMakeTmp/DetermineSoundLibs.c
    "#include \"SDL_sound.h\"
    #include \"SDL.h\"
    int main(int argc, char* argv[])
    {
        Sound_AudioInfo desired;
        Sound_Sample* sample;

        SDL_Init(0);
        Sound_Init();

        /* This doesn't actually have to work, but Init() is a no-op
         * for some of the decoders, so this should force more symbols
         * to be pulled in.
         */
        sample = Sound_NewSampleFromFile(argv[1], &desired, 4096);

        Sound_Quit();
        SDL_Quit();
        return 0;
     }"
  )

  # Calling
  # target_link_libraries(DetermineSoundLibs "${SDL_SOUND_LIBRARY} ${SDL_LIBRARY})
  # causes problems when SDL_LIBRARY looks like
  # /Library/Frameworks/SDL.framework;-framework Cocoa
  # The ;-framework Cocoa seems to be confusing CMake once the OS X
  # framework support was added. I was told that breaking up the list
  # would fix the problem.
  set(TMP_TRY_LIBS)
  foreach(lib ${SDL_SOUND_LIBRARY} ${SDL_LIBRARY})
    string(APPEND TMP_TRY_LIBS " \"${lib}\"")
  endforeach()


  # Write the CMakeLists.txt and test project
  # Weird, this is still sketchy. If I don't quote the variables
  # in the TARGET_LINK_LIBRARIES, I seem to loose everything
  # in the SDL_LIBRARY string after the "-framework".
  # But if I quote the stuff in INCLUDE_DIRECTORIES, it doesn't work.
  file(WRITE ${PROJECT_BINARY_DIR}/CMakeTmp/CMakeLists.txt
    "cmake_minimum_required(VERSION ${CMAKE_VERSION})
     project(DetermineSoundLibs)
     include_directories(${SDL_INCLUDE_DIR} ${SDL_SOUND_INCLUDE_DIR})
     add_executable(DetermineSoundLibs DetermineSoundLibs.c)
     target_link_libraries(DetermineSoundLibs ${TMP_TRY_LIBS})"
    )

  try_compile(
    MY_RESULT
    ${PROJECT_BINARY_DIR}/CMakeTmp
    ${PROJECT_BINARY_DIR}/CMakeTmp
    DetermineSoundLibs
    OUTPUT_VARIABLE MY_OUTPUT
    )


  if(NOT MY_RESULT)

    # I expect that MPGLIB, VOC, WAV, AIFF, and SHN are compiled in statically.
    # I think Timidity is also compiled in statically.
    # I've never had to explcitly link against Quicktime, so I'll skip that for now.

    set(SDL_SOUND_LIBRARIES_TMP ${SDL_SOUND_LIBRARY})

    # Find MikMod
    if("${MY_OUTPUT}" MATCHES "MikMod_")
      find_library(MIKMOD_LIBRARY
        NAMES libmikmod-coreaudio mikmod
        PATHS
          ENV MIKMODDIR
          ENV SDLSOUNDDIR
          ENV SDLDIR
          /opt
        PATH_SUFFIXES
          lib
      )
      if(MIKMOD_LIBRARY)
        set(SDL_SOUND_LIBRARIES_TMP ${SDL_SOUND_LIBRARIES_TMP} ${MIKMOD_LIBRARY})
      endif(MIKMOD_LIBRARY)
    endif("${MY_OUTPUT}" MATCHES "MikMod_")

    # Find ModPlug
    if("${MY_OUTPUT}" MATCHES "MODPLUG_")
      find_library(MODPLUG_LIBRARY
        NAMES modplug
        PATHS
          ENV MODPLUGDIR
          ENV SDLSOUNDDIR
          ENV SDLDIR
          /opt
        PATH_SUFFIXES
          lib
      )
      if(MODPLUG_LIBRARY)
        set(SDL_SOUND_LIBRARIES_TMP ${SDL_SOUND_LIBRARIES_TMP} ${MODPLUG_LIBRARY})
      endif()
    endif()


    # Find Ogg and Vorbis
    if("${MY_OUTPUT}" MATCHES "ov_")
      find_library(VORBIS_LIBRARY
        NAMES vorbis Vorbis VORBIS
        PATHS
          ENV VORBISDIR
          ENV OGGDIR
          ENV SDLSOUNDDIR
          ENV SDLDIR
          /opt
        PATH_SUFFIXES
          lib
        )
      if(VORBIS_LIBRARY)
        set(SDL_SOUND_LIBRARIES_TMP ${SDL_SOUND_LIBRARIES_TMP} ${VORBIS_LIBRARY})
      endif()

      find_library(OGG_LIBRARY
        NAMES ogg Ogg OGG
        PATHS
          ENV OGGDIR
          ENV VORBISDIR
          ENV SDLSOUNDDIR
          ENV SDLDIR
          /opt
        PATH_SUFFIXES
          lib
         )
      if(OGG_LIBRARY)
        set(SDL_SOUND_LIBRARIES_TMP ${SDL_SOUND_LIBRARIES_TMP} ${OGG_LIBRARY})
      endif()
    endif()


    # Find SMPEG
    if("${MY_OUTPUT}" MATCHES "SMPEG_")
      find_library(SMPEG_LIBRARY
        NAMES smpeg SMPEG Smpeg SMpeg
        PATHS
          ENV SMPEGDIR
          ENV SDLSOUNDDIR
          ENV SDLDIR
          /opt
        PATH_SUFFIXES
          lib
        )
      if(SMPEG_LIBRARY)
        set(SDL_SOUND_LIBRARIES_TMP ${SDL_SOUND_LIBRARIES_TMP} ${SMPEG_LIBRARY})
      endif()
    endif()


    # Find FLAC
    if("${MY_OUTPUT}" MATCHES "FLAC_")
      find_library(FLAC_LIBRARY
        NAMES flac FLAC
        PATHS
          ENV FLACDIR
          ENV SDLSOUNDDIR
          ENV SDLDIR
          /opt
        PATH_SUFFIXES
          lib
        )
      if(FLAC_LIBRARY)
        set(SDL_SOUND_LIBRARIES_TMP ${SDL_SOUND_LIBRARIES_TMP} ${FLAC_LIBRARY})
      endif()
    endif()


    # Hmmm...Speex seems to depend on Ogg. This might be a problem if
    # the TRY_COMPILE attempt gets blocked at SPEEX before it can pull
    # in the Ogg symbols. I'm not sure if I should duplicate the ogg stuff
    # above for here or if two ogg entries will screw up things.
    if("${MY_OUTPUT}" MATCHES "speex_")
      find_library(SPEEX_LIBRARY
        NAMES speex SPEEX
        PATHS
          ENV SPEEXDIR
          ENV SDLSOUNDDIR
          ENV SDLDIR
          /opt
        PATH_SUFFIXES
          lib
        )
      if(SPEEX_LIBRARY)
        set(SDL_SOUND_LIBRARIES_TMP ${SDL_SOUND_LIBRARIES_TMP} ${SPEEX_LIBRARY})
      endif()

      # Find OGG (needed for Speex)
      # We might have already found Ogg for Vorbis, so skip it if so.
      if(NOT OGG_LIBRARY)
        find_library(OGG_LIBRARY
          NAMES ogg Ogg OGG
          PATHS
            ENV OGGDIR
            ENV VORBISDIR
            ENV SPEEXDIR
            ENV SDLSOUNDDIR
            ENV SDLDIR
            /opt
          PATH_SUFFIXES lib
          )
        if(OGG_LIBRARY)
          set(SDL_SOUND_LIBRARIES_TMP ${SDL_SOUND_LIBRARIES_TMP} ${OGG_LIBRARY})
        endif()
      endif()
    endif()

    set(SDL_SOUND_LIBRARIES ${SDL_SOUND_EXTRAS} ${SDL_SOUND_LIBRARIES_TMP})
  else()
    set(SDL_SOUND_LIBRARIES ${SDL_SOUND_EXTRAS} ${SDL_SOUND_LIBRARY})
  endif()
 endif()

if(SDL_SOUND_INCLUDE_DIR AND EXISTS "${SDL_SOUND_INCLUDE_DIR}/SDL_sound.h")
  file(STRINGS "${SDL_SOUND_INCLUDE_DIR}/SDL_sound.h" SDL_SOUND_VERSION_MAJOR_LINE REGEX "^#define[ \t]+SOUND_VER_MAJOR[ \t]+[0-9]+$")
  file(STRINGS "${SDL_SOUND_INCLUDE_DIR}/SDL_sound.h" SDL_SOUND_VERSION_MINOR_LINE REGEX "^#define[ \t]+SOUND_VER_MINOR[ \t]+[0-9]+$")
  file(STRINGS "${SDL_SOUND_INCLUDE_DIR}/SDL_sound.h" SDL_SOUND_VERSION_PATCH_LINE REGEX "^#define[ \t]+SOUND_VER_PATCH[ \t]+[0-9]+$")
  string(REGEX REPLACE "^#define[ \t]+SOUND_VER_MAJOR[ \t]+([0-9]+)$" "\\1" SDL_SOUND_VERSION_MAJOR "${SDL_SOUND_VERSION_MAJOR_LINE}")
  string(REGEX REPLACE "^#define[ \t]+SOUND_VER_MINOR[ \t]+([0-9]+)$" "\\1" SDL_SOUND_VERSION_MINOR "${SDL_SOUND_VERSION_MINOR_LINE}")
  string(REGEX REPLACE "^#define[ \t]+SOUND_VER_PATCH[ \t]+([0-9]+)$" "\\1" SDL_SOUND_VERSION_PATCH "${SDL_SOUND_VERSION_PATCH_LINE}")
  set(SDL_SOUND_VERSION_STRING ${SDL_SOUND_VERSION_MAJOR}.${SDL_SOUND_VERSION_MINOR}.${SDL_SOUND_VERSION_PATCH})
  unset(SDL_SOUND_VERSION_MAJOR_LINE)
  unset(SDL_SOUND_VERSION_MINOR_LINE)
  unset(SDL_SOUND_VERSION_PATCH_LINE)
  unset(SDL_SOUND_VERSION_MAJOR)
  unset(SDL_SOUND_VERSION_MINOR)
  unset(SDL_SOUND_VERSION_PATCH)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(SDL_sound
                                  REQUIRED_VARS SDL_SOUND_LIBRARY SDL_SOUND_INCLUDE_DIR
                                  VERSION_VAR SDL_SOUND_VERSION_STRING)
