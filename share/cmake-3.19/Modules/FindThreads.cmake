# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindThreads
-----------

This module determines the thread library of the system.

Imported Targets
^^^^^^^^^^^^^^^^

This module defines the following :prop_tgt:`IMPORTED` target:

``Threads::Threads``
  The thread library, if found.

Result Variables
^^^^^^^^^^^^^^^^

The following variables are set:

``Threads_FOUND``
  If a supported thread library was found.
``CMAKE_THREAD_LIBS_INIT``
  The thread library to use. This may be empty if the thread functions
  are provided by the system libraries and no special flags are needed
  to use them.
``CMAKE_USE_WIN32_THREADS_INIT``
  If the found thread library is the win32 one.
``CMAKE_USE_PTHREADS_INIT``
  If the found thread library is pthread compatible.
``CMAKE_HP_PTHREADS_INIT``
  If the found thread library is the HP thread library.

Variables Affecting Behavior
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. variable:: THREADS_PREFER_PTHREAD_FLAG

  If the use of the -pthread compiler and linker flag is preferred then
  the caller can set this variable to TRUE. The compiler flag can only be
  used with the imported target. Use of both the imported target as well
  as this switch is highly recommended for new code.

  This variable has no effect if the system libraries provide the
  thread functions, i.e. when ``CMAKE_THREAD_LIBS_INIT`` will be empty.
#]=======================================================================]

include (CheckLibraryExists)
set(Threads_FOUND FALSE)
set(CMAKE_REQUIRED_QUIET_SAVE ${CMAKE_REQUIRED_QUIET})
set(CMAKE_REQUIRED_QUIET ${Threads_FIND_QUIETLY})

if(CMAKE_C_COMPILER_LOADED)
  include (CheckIncludeFile)
  include (CheckCSourceCompiles)
elseif(CMAKE_CXX_COMPILER_LOADED)
  include (CheckIncludeFileCXX)
  include (CheckCXXSourceCompiles)
else()
  message(FATAL_ERROR "FindThreads only works if either C or CXX language is enabled")
endif()

# simple pthread test code
set(PTHREAD_C_CXX_TEST_SOURCE [====[
#include <pthread.h>

static void* test_func(void* data)
{
  return data;
}

int main(void)
{
  pthread_t thread;
  pthread_create(&thread, NULL, test_func, NULL);
  pthread_detach(thread);
  pthread_cancel(thread);
  pthread_join(thread, NULL);
  pthread_atfork(NULL, NULL, NULL);
  pthread_exit(NULL);

  return 0;
}
]====])

# Internal helper macro.
# Do NOT even think about using it outside of this file!
macro(_check_threads_lib LIBNAME FUNCNAME VARNAME)
  if(NOT Threads_FOUND)
     CHECK_LIBRARY_EXISTS(${LIBNAME} ${FUNCNAME} "" ${VARNAME})
     if(${VARNAME})
       set(CMAKE_THREAD_LIBS_INIT "-l${LIBNAME}")
       set(CMAKE_HAVE_THREADS_LIBRARY 1)
       set(Threads_FOUND TRUE)
     endif()
  endif ()
endmacro()

# Internal helper macro.
# Do NOT even think about using it outside of this file!
macro(_check_pthreads_flag)
  if(NOT Threads_FOUND)
    # If we did not found -lpthread, -lpthread, or -lthread, look for -pthread
    if(NOT DEFINED THREADS_HAVE_PTHREAD_ARG)
      message(CHECK_START "Check if compiler accepts -pthread")
      if(CMAKE_C_COMPILER_LOADED)
        set(_threads_src ${CMAKE_CURRENT_LIST_DIR}/CheckForPthreads.c)
      elseif(CMAKE_CXX_COMPILER_LOADED)
        set(_threads_src ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/FindThreads/CheckForPthreads.cxx)
        configure_file(${CMAKE_CURRENT_LIST_DIR}/CheckForPthreads.c "${_threads_src}" COPYONLY)
      endif()
      try_compile(THREADS_HAVE_PTHREAD_ARG
        ${CMAKE_BINARY_DIR}
        ${_threads_src}
        CMAKE_FLAGS -DLINK_LIBRARIES:STRING=-pthread
        OUTPUT_VARIABLE OUTPUT)
      unset(_threads_src)

      if(THREADS_HAVE_PTHREAD_ARG)
        set(Threads_FOUND TRUE)
        message(CHECK_PASS "yes")
      else()
        message(CHECK_FAIL "no")
        file(APPEND
          ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
          "Determining if compiler accepts -pthread failed with the following output:\n${OUTPUT}\n\n")
      endif()

    endif()

    if(THREADS_HAVE_PTHREAD_ARG)
      set(Threads_FOUND TRUE)
      set(CMAKE_THREAD_LIBS_INIT "-pthread")
    endif()
  endif()
endmacro()

# Do we have pthreads?
if(CMAKE_C_COMPILER_LOADED)
  CHECK_INCLUDE_FILE("pthread.h" CMAKE_HAVE_PTHREAD_H)
else()
  CHECK_INCLUDE_FILE_CXX("pthread.h" CMAKE_HAVE_PTHREAD_H)
endif()

if(CMAKE_HAVE_PTHREAD_H)
  #
  # We have pthread.h
  # Let's check for the library now.
  #
  set(CMAKE_HAVE_THREADS_LIBRARY)
  if(NOT THREADS_HAVE_PTHREAD_ARG)
    # Check if pthread functions are in normal C library.
    # We list some pthread functions in PTHREAD_C_CXX_TEST_SOURCE test code.
    # If the pthread functions already exist in C library, we could just use
    # them instead of linking to the additional pthread library.
    if(CMAKE_C_COMPILER_LOADED)
      CHECK_C_SOURCE_COMPILES("${PTHREAD_C_CXX_TEST_SOURCE}" CMAKE_HAVE_LIBC_PTHREAD)
    elseif(CMAKE_CXX_COMPILER_LOADED)
      CHECK_CXX_SOURCE_COMPILES("${PTHREAD_C_CXX_TEST_SOURCE}" CMAKE_HAVE_LIBC_PTHREAD)
    endif()
    if(CMAKE_HAVE_LIBC_PTHREAD)
      set(CMAKE_THREAD_LIBS_INIT "")
      set(CMAKE_HAVE_THREADS_LIBRARY 1)
      set(Threads_FOUND TRUE)
    else()
      # Check for -pthread first if enabled. This is the recommended
      # way, but not backwards compatible as one must also pass -pthread
      # as compiler flag then.
      if (THREADS_PREFER_PTHREAD_FLAG)
         _check_pthreads_flag()
      endif ()

      if(CMAKE_SYSTEM MATCHES "GHS-MULTI")
        _check_threads_lib(posix pthread_create CMAKE_HAVE_PTHREADS_CREATE)
      endif()
      _check_threads_lib(pthreads pthread_create CMAKE_HAVE_PTHREADS_CREATE)
      _check_threads_lib(pthread  pthread_create CMAKE_HAVE_PTHREAD_CREATE)
      if(CMAKE_SYSTEM_NAME MATCHES "SunOS")
          # On sun also check for -lthread
          _check_threads_lib(thread thr_create CMAKE_HAVE_THR_CREATE)
      endif()
    endif()
  endif()

  _check_pthreads_flag()
endif()

if(CMAKE_THREAD_LIBS_INIT OR CMAKE_HAVE_LIBC_PTHREAD)
  set(CMAKE_USE_PTHREADS_INIT 1)
  set(Threads_FOUND TRUE)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  set(CMAKE_USE_WIN32_THREADS_INIT 1)
  set(Threads_FOUND TRUE)
endif()

if(CMAKE_USE_PTHREADS_INIT)
  if(CMAKE_SYSTEM_NAME MATCHES "HP-UX")
    # Use libcma if it exists and can be used.  It provides more
    # symbols than the plain pthread library.  CMA threads
    # have actually been deprecated:
    #   http://docs.hp.com/en/B3920-90091/ch12s03.html#d0e11395
    #   http://docs.hp.com/en/947/d8.html
    # but we need to maintain compatibility here.
    # The CMAKE_HP_PTHREADS setting actually indicates whether CMA threads
    # are available.
    CHECK_LIBRARY_EXISTS(cma pthread_attr_create "" CMAKE_HAVE_HP_CMA)
    if(CMAKE_HAVE_HP_CMA)
      set(CMAKE_THREAD_LIBS_INIT "-lcma")
      set(CMAKE_HP_PTHREADS_INIT 1)
      set(Threads_FOUND TRUE)
    endif()
    set(CMAKE_USE_PTHREADS_INIT 1)
  endif()

  if(CMAKE_SYSTEM MATCHES "OSF1-V")
    set(CMAKE_USE_PTHREADS_INIT 0)
    set(CMAKE_THREAD_LIBS_INIT )
  endif()

  if(CMAKE_SYSTEM MATCHES "CYGWIN_NT")
    set(CMAKE_USE_PTHREADS_INIT 1)
    set(Threads_FOUND TRUE)
    set(CMAKE_THREAD_LIBS_INIT )
    set(CMAKE_USE_WIN32_THREADS_INIT 0)
  endif()
endif()

set(CMAKE_REQUIRED_QUIET ${CMAKE_REQUIRED_QUIET_SAVE})
include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Threads DEFAULT_MSG Threads_FOUND)

if(THREADS_FOUND AND NOT TARGET Threads::Threads)
  add_library(Threads::Threads INTERFACE IMPORTED)

  if(THREADS_HAVE_PTHREAD_ARG)
    set_property(TARGET Threads::Threads
                 PROPERTY INTERFACE_COMPILE_OPTIONS "$<$<COMPILE_LANG_AND_ID:CUDA,NVIDIA>:SHELL:-Xcompiler -pthread>"
                                                    "$<$<NOT:$<COMPILE_LANG_AND_ID:CUDA,NVIDIA>>:-pthread>")
  endif()

  if(CMAKE_THREAD_LIBS_INIT)
    set_property(TARGET Threads::Threads PROPERTY INTERFACE_LINK_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}")
  endif()
endif()
