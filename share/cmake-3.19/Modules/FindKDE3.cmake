# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindKDE3
--------

Find the KDE3 include and library dirs, KDE preprocessors and define a some macros



This module defines the following variables:

``KDE3_DEFINITIONS``
  compiler definitions required for compiling KDE software
``KDE3_INCLUDE_DIR``
  the KDE include directory
``KDE3_INCLUDE_DIRS``
  the KDE and the Qt include directory, for use with include_directories()
``KDE3_LIB_DIR``
  the directory where the KDE libraries are installed, for use with link_directories()
``QT_AND_KDECORE_LIBS``
  this contains both the Qt and the kdecore library
``KDE3_DCOPIDL_EXECUTABLE``
  the dcopidl executable
``KDE3_DCOPIDL2CPP_EXECUTABLE``
  the dcopidl2cpp executable
``KDE3_KCFGC_EXECUTABLE``
  the kconfig_compiler executable
``KDE3_FOUND``
  set to TRUE if all of the above has been found

The following user adjustable options are provided:

``KDE3_BUILD_TESTS``
  enable this to build KDE testcases

It also adds the following macros (from ``KDE3Macros.cmake``) ``SRCS_VAR`` is
always the variable which contains the list of source files for your
application or library.

KDE3_AUTOMOC(file1 ...  fileN)

::

    Call this if you want to have automatic moc file handling.
    This means if you include "foo.moc" in the source file foo.cpp
    a moc file for the header foo.h will be created automatically.
    You can set the property SKIP_AUTOMAKE using set_source_files_properties()
    to exclude some files in the list from being processed.



KDE3_ADD_MOC_FILES(SRCS_VAR file1 ...  fileN )

::

    If you don't use the KDE3_AUTOMOC() macro, for the files
    listed here moc files will be created (named "foo.moc.cpp")



KDE3_ADD_DCOP_SKELS(SRCS_VAR header1.h ...  headerN.h )

::

    Use this to generate DCOP skeletions from the listed headers.



KDE3_ADD_DCOP_STUBS(SRCS_VAR header1.h ...  headerN.h )

::

     Use this to generate DCOP stubs from the listed headers.



KDE3_ADD_UI_FILES(SRCS_VAR file1.ui ...  fileN.ui )

::

    Use this to add the Qt designer ui files to your application/library.



KDE3_ADD_KCFG_FILES(SRCS_VAR file1.kcfgc ...  fileN.kcfgc )

::

    Use this to add KDE kconfig compiler files to your application/library.



KDE3_INSTALL_LIBTOOL_FILE(target)

::

    This will create and install a simple libtool file for the given target.



KDE3_ADD_EXECUTABLE(name file1 ...  fileN )

::

    Currently identical to add_executable(), may provide some advanced
    features in the future.



KDE3_ADD_KPART(name [WITH_PREFIX] file1 ...  fileN )

::

    Create a KDE plugin (KPart, kioslave, etc.) from the given source files.
    If WITH_PREFIX is given, the resulting plugin will have the prefix "lib",
    otherwise it won't.
    It creates and installs an appropriate libtool la-file.



KDE3_ADD_KDEINIT_EXECUTABLE(name file1 ...  fileN )

::

    Create a KDE application in the form of a module loadable via kdeinit.
    A library named kdeinit_<name> will be created and a small executable
    which links to it.



The option KDE3_ENABLE_FINAL to enable all-in-one compilation is no
longer supported.



Author: Alexander Neundorf <neundorf@kde.org>
#]=======================================================================]

if(NOT UNIX AND KDE3_FIND_REQUIRED)
  message(FATAL_ERROR "Compiling KDE3 applications and libraries under Windows is not supported")
endif()

# If Qt4 has already been found, fail.
if(QT4_FOUND)
  if(KDE3_FIND_REQUIRED)
    message( FATAL_ERROR "KDE3/Qt3 and Qt4 cannot be used together in one project.")
  else()
    if(NOT KDE3_FIND_QUIETLY)
      message( STATUS    "KDE3/Qt3 and Qt4 cannot be used together in one project.")
    endif()
    return()
  endif()
endif()


set(QT_MT_REQUIRED TRUE)
#set(QT_MIN_VERSION "3.0.0")

#this line includes FindQt.cmake, which searches the Qt library and headers
if(KDE3_FIND_REQUIRED)
  set(_REQ_STRING_KDE3 "REQUIRED")
endif()

find_package(Qt3 ${_REQ_STRING_KDE3})
find_package(X11 ${_REQ_STRING_KDE3})


#now try to find some kde stuff
find_program(KDECONFIG_EXECUTABLE NAMES kde-config
  HINTS
    $ENV{KDEDIR}/bin
  PATHS
    /opt/kde3/bin
    /opt/kde/bin
  )

set(KDE3PREFIX)
if(KDECONFIG_EXECUTABLE)
  execute_process(COMMAND ${KDECONFIG_EXECUTABLE} --version
                  OUTPUT_VARIABLE kde_config_version )

  string(REGEX MATCH "KDE: .\\." kde_version "${kde_config_version}")
  if ("${kde_version}" MATCHES "KDE: 3\\.")
    execute_process(COMMAND ${KDECONFIG_EXECUTABLE} --prefix
                    OUTPUT_VARIABLE kdedir )
    string(REPLACE "\n" "" KDE3PREFIX "${kdedir}")

  endif ()
endif()



# at first the KDE include directory
# kpassdlg.h comes from kdeui and doesn't exist in KDE4 anymore
find_path(KDE3_INCLUDE_DIR kpassdlg.h
  HINTS
    $ENV{KDEDIR}/include
    ${KDE3PREFIX}/include
  PATHS
    /opt/kde3/include
    /opt/kde/include
  PATH_SUFFIXES include/kde
  )

#now the KDE library directory
find_library(KDE3_KDECORE_LIBRARY NAMES kdecore
  HINTS
    $ENV{KDEDIR}/lib
    ${KDE3PREFIX}/lib
  PATHS
    /opt/kde3/lib
    /opt/kde/lib
)

set(QT_AND_KDECORE_LIBS ${QT_LIBRARIES} ${KDE3_KDECORE_LIBRARY})

get_filename_component(KDE3_LIB_DIR ${KDE3_KDECORE_LIBRARY} PATH )

if(NOT KDE3_LIBTOOL_DIR)
  if(KDE3_KDECORE_LIBRARY MATCHES lib64)
    set(KDE3_LIBTOOL_DIR /lib64/kde3)
  elseif(KDE3_KDECORE_LIBRARY MATCHES libx32)
    set(KDE3_LIBTOOL_DIR /libx32/kde3)
  else()
    set(KDE3_LIBTOOL_DIR /lib/kde3)
  endif()
endif()

#now search for the dcop utilities
find_program(KDE3_DCOPIDL_EXECUTABLE NAMES dcopidl
  HINTS
    $ENV{KDEDIR}/bin
    ${KDE3PREFIX}/bin
  PATHS
    /opt/kde3/bin
    /opt/kde/bin
  )

find_program(KDE3_DCOPIDL2CPP_EXECUTABLE NAMES dcopidl2cpp
  HINTS
    $ENV{KDEDIR}/bin
    ${KDE3PREFIX}/bin
  PATHS
    /opt/kde3/bin
    /opt/kde/bin
  )

find_program(KDE3_KCFGC_EXECUTABLE NAMES kconfig_compiler
  HINTS
    $ENV{KDEDIR}/bin
    ${KDE3PREFIX}/bin
  PATHS
    /opt/kde3/bin
    /opt/kde/bin
  )


#SET KDE3_FOUND
if (KDE3_INCLUDE_DIR AND KDE3_LIB_DIR AND KDE3_DCOPIDL_EXECUTABLE AND KDE3_DCOPIDL2CPP_EXECUTABLE AND KDE3_KCFGC_EXECUTABLE)
  set(KDE3_FOUND TRUE)
else ()
  set(KDE3_FOUND FALSE)
endif ()

# add some KDE specific stuff
set(KDE3_DEFINITIONS -DQT_CLEAN_NAMESPACE -D_GNU_SOURCE)

# set compiler flags only if KDE3 has actually been found
if(KDE3_FOUND)
  set(_KDE3_USE_FLAGS FALSE)
  if(CMAKE_COMPILER_IS_GNUCXX)
    set(_KDE3_USE_FLAGS TRUE) # use flags for gnu compiler
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} --version
                    OUTPUT_VARIABLE out)
    # gnu gcc 2.96 does not work with flags
    # I guess 2.95 also doesn't then
    if("${out}" MATCHES "2.9[56]")
      set(_KDE3_USE_FLAGS FALSE)
    endif()
  endif()

  #only on linux, but NOT e.g. on FreeBSD:
  if(CMAKE_SYSTEM_NAME MATCHES "Linux" AND _KDE3_USE_FLAGS)
    set (KDE3_DEFINITIONS ${KDE3_DEFINITIONS} -D_XOPEN_SOURCE=500 -D_BSD_SOURCE)
    set ( CMAKE_C_FLAGS     "${CMAKE_C_FLAGS} -Wno-long-long -ansi -Wundef -Wcast-align -Wconversion -Wchar-subscripts -Wall -W -Wpointer-arith -Wwrite-strings -Wformat-security -Wmissing-format-attribute -fno-common")
    set ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wnon-virtual-dtor -Wno-long-long -ansi -Wundef -Wcast-align -Wconversion -Wchar-subscripts -Wall -W -Wpointer-arith -Wwrite-strings -Wformat-security -fno-exceptions -fno-check-new -fno-common")
  endif()

  # works on FreeBSD, NOT tested on NetBSD and OpenBSD
  if (CMAKE_SYSTEM_NAME MATCHES BSD AND _KDE3_USE_FLAGS)
    set ( CMAKE_C_FLAGS     "${CMAKE_C_FLAGS} -Wno-long-long -ansi -Wundef -Wcast-align -Wconversion -Wchar-subscripts -Wall -W -Wpointer-arith -Wwrite-strings -Wformat-security -Wmissing-format-attribute -fno-common")
    set ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wnon-virtual-dtor -Wno-long-long -Wundef -Wcast-align -Wconversion -Wchar-subscripts -Wall -W -Wpointer-arith -Wwrite-strings -Wformat-security -Wmissing-format-attribute -fno-exceptions -fno-check-new -fno-common")
  endif ()

  # if no special buildtype is selected, add -O2 as default optimization
  if (NOT CMAKE_BUILD_TYPE AND _KDE3_USE_FLAGS)
    set ( CMAKE_C_FLAGS     "${CMAKE_C_FLAGS} -O2")
    set ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
  endif ()

#set(CMAKE_SHARED_LINKER_FLAGS "-avoid-version -module -Wl,--no-undefined -Wl,--allow-shlib-undefined")
#set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--fatal-warnings -avoid-version -Wl,--no-undefined -lc")
#set(CMAKE_MODULE_LINKER_FLAGS "-Wl,--fatal-warnings -avoid-version -Wl,--no-undefined -lc")
endif()


# KDE3Macros.cmake contains all the KDE specific macros
include(${CMAKE_CURRENT_LIST_DIR}/KDE3Macros.cmake)


macro (KDE3_PRINT_RESULTS)
  if(KDE3_INCLUDE_DIR)
    message(STATUS "Found KDE3 include dir: ${KDE3_INCLUDE_DIR}")
  else()
    message(STATUS "Didn't find KDE3 headers")
  endif()

  if(KDE3_LIB_DIR)
    message(STATUS "Found KDE3 library dir: ${KDE3_LIB_DIR}")
  else()
    message(STATUS "Didn't find KDE3 core library")
  endif()

  if(KDE3_DCOPIDL_EXECUTABLE)
    message(STATUS "Found KDE3 dcopidl preprocessor: ${KDE3_DCOPIDL_EXECUTABLE}")
  else()
    message(STATUS "Didn't find the KDE3 dcopidl preprocessor")
  endif()

  if(KDE3_DCOPIDL2CPP_EXECUTABLE)
    message(STATUS "Found KDE3 dcopidl2cpp preprocessor: ${KDE3_DCOPIDL2CPP_EXECUTABLE}")
  else()
    message(STATUS "Didn't find the KDE3 dcopidl2cpp preprocessor")
  endif()

  if(KDE3_KCFGC_EXECUTABLE)
    message(STATUS "Found KDE3 kconfig_compiler preprocessor: ${KDE3_KCFGC_EXECUTABLE}")
  else()
    message(STATUS "Didn't find the KDE3 kconfig_compiler preprocessor")
  endif()

endmacro ()


if (KDE3_FIND_REQUIRED AND NOT KDE3_FOUND)
  #bail out if something wasn't found
  KDE3_PRINT_RESULTS()
  message(FATAL_ERROR "Could NOT find everything required for compiling KDE 3 programs")

endif ()


if (NOT KDE3_FIND_QUIETLY)
  KDE3_PRINT_RESULTS()
endif ()

#add the found Qt and KDE include directories to the current include path
set(KDE3_INCLUDE_DIRS ${QT_INCLUDE_DIR} ${KDE3_INCLUDE_DIR})
