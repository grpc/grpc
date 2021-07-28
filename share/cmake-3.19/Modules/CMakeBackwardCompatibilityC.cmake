# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.


if(NOT CMAKE_SKIP_COMPATIBILITY_TESTS)
  # Old CMake versions did not support OS X universal binaries anyway,
  # so just get through this with at least some size for the types.
  list(LENGTH CMAKE_OSX_ARCHITECTURES NUM_ARCHS)
  if(${NUM_ARCHS} GREATER 1)
    if(NOT DEFINED CMAKE_TRY_COMPILE_OSX_ARCHITECTURES)
      message(WARNING "This module does not work with OS X universal binaries.")
      set(__ERASE_CMAKE_TRY_COMPILE_OSX_ARCHITECTURES 1)
      list(GET CMAKE_OSX_ARCHITECTURES 0 CMAKE_TRY_COMPILE_OSX_ARCHITECTURES)
    endif()
  endif()

  include (CheckTypeSize)
  CHECK_TYPE_SIZE(int      CMAKE_SIZEOF_INT)
  CHECK_TYPE_SIZE(long     CMAKE_SIZEOF_LONG)
  CHECK_TYPE_SIZE("void*"  CMAKE_SIZEOF_VOID_P)
  CHECK_TYPE_SIZE(char     CMAKE_SIZEOF_CHAR)
  CHECK_TYPE_SIZE(short    CMAKE_SIZEOF_SHORT)
  CHECK_TYPE_SIZE(float    CMAKE_SIZEOF_FLOAT)
  CHECK_TYPE_SIZE(double   CMAKE_SIZEOF_DOUBLE)

  include (CheckIncludeFile)
  CHECK_INCLUDE_FILE("limits.h"       CMAKE_HAVE_LIMITS_H)
  CHECK_INCLUDE_FILE("unistd.h"       CMAKE_HAVE_UNISTD_H)
  CHECK_INCLUDE_FILE("pthread.h"      CMAKE_HAVE_PTHREAD_H)

  include (CheckIncludeFiles)
  CHECK_INCLUDE_FILES("sys/types.h;sys/prctl.h"    CMAKE_HAVE_SYS_PRCTL_H)

  include (TestBigEndian)
  TEST_BIG_ENDIAN(CMAKE_WORDS_BIGENDIAN)
  include (FindX11)

  if("${X11_X11_INCLUDE_PATH}" STREQUAL "/usr/include")
    set (CMAKE_X_CFLAGS "" CACHE STRING "X11 extra flags.")
  else()
    set (CMAKE_X_CFLAGS "-I${X11_X11_INCLUDE_PATH}" CACHE STRING
         "X11 extra flags.")
  endif()
  set (CMAKE_X_LIBS "${X11_LIBRARIES}" CACHE STRING
       "Libraries and options used in X11 programs.")
  set (CMAKE_HAS_X "${X11_FOUND}" CACHE INTERNAL "Is X11 around.")

  include (FindThreads)

  set (CMAKE_THREAD_LIBS        "${CMAKE_THREAD_LIBS_INIT}" CACHE STRING
    "Thread library used.")

  set (CMAKE_USE_PTHREADS       "${CMAKE_USE_PTHREADS_INIT}" CACHE BOOL
     "Use the pthreads library.")

  set (CMAKE_USE_WIN32_THREADS  "${CMAKE_USE_WIN32_THREADS_INIT}" CACHE BOOL
       "Use the win32 thread library.")

  set (CMAKE_HP_PTHREADS        ${CMAKE_HP_PTHREADS_INIT} CACHE BOOL
     "Use HP pthreads.")

  if(__ERASE_CMAKE_TRY_COMPILE_OSX_ARCHITECTURES)
    set(CMAKE_TRY_COMPILE_OSX_ARCHITECTURES)
    set(__ERASE_CMAKE_TRY_COMPILE_OSX_ARCHITECTURES)
  endif()
endif()

mark_as_advanced(
CMAKE_HP_PTHREADS
CMAKE_THREAD_LIBS
CMAKE_USE_PTHREADS
CMAKE_USE_WIN32_THREADS
CMAKE_X_CFLAGS
CMAKE_X_LIBS
)

