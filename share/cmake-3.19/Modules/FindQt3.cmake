# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindQt3
-------

Locate Qt include paths and libraries

This module defines:

::

  QT_INCLUDE_DIR    - where to find qt.h, etc.
  QT_LIBRARIES      - the libraries to link against to use Qt.
  QT_DEFINITIONS    - definitions to use when
                      compiling code that uses Qt.
  QT_FOUND          - If false, don't try to use Qt.
  QT_VERSION_STRING - the version of Qt found



If you need the multithreaded version of Qt, set QT_MT_REQUIRED to
TRUE

Also defined, but not for general use are:

::

  QT_MOC_EXECUTABLE, where to find the moc tool.
  QT_UIC_EXECUTABLE, where to find the uic tool.
  QT_QT_LIBRARY, where to find the Qt library.
  QT_QTMAIN_LIBRARY, where to find the qtmain
   library. This is only required by Qt3 on Windows.
#]=======================================================================]

# These are around for backwards compatibility
# they will be set
#  QT_WRAP_CPP, set true if QT_MOC_EXECUTABLE is found
#  QT_WRAP_UI set true if QT_UIC_EXECUTABLE is found

# If Qt4 has already been found, fail.
if(QT4_FOUND)
  if(Qt3_FIND_REQUIRED)
    message( FATAL_ERROR "Qt3 and Qt4 cannot be used together in one project.")
  else()
    if(NOT Qt3_FIND_QUIETLY)
      message( STATUS    "Qt3 and Qt4 cannot be used together in one project.")
    endif()
    return()
  endif()
endif()


file(GLOB GLOB_PATHS /usr/lib/qt-3*)
foreach(GLOB_PATH ${GLOB_PATHS})
  list(APPEND GLOB_PATHS_BIN "${GLOB_PATH}/bin")
endforeach()
find_path(QT_INCLUDE_DIR qt.h
  "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.1;InstallDir]/include/Qt"
  "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.0;InstallDir]/include/Qt"
  "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.1.0;InstallDir]/include/Qt"
  $ENV{QTDIR}/include
  ${GLOB_PATHS}
  /usr/share/qt3/include
  C:/Progra~1/qt/include
  /usr/local/include/X11/qt3
  PATH_SUFFIXES lib/qt/include lib/qt3/include include/qt include/qt3 qt/include qt3/include
  )

# if qglobal.h is not in the qt_include_dir then set
# QT_INCLUDE_DIR to NOTFOUND
if(NOT EXISTS ${QT_INCLUDE_DIR}/qglobal.h)
  set(QT_INCLUDE_DIR QT_INCLUDE_DIR-NOTFOUND CACHE PATH "path to Qt3 include directory" FORCE)
endif()

if(QT_INCLUDE_DIR)
  #extract the version string from qglobal.h
  file(STRINGS ${QT_INCLUDE_DIR}/qglobal.h QGLOBAL_H REGEX "#define[\t ]+QT_VERSION_STR[\t ]+\"[0-9]+.[0-9]+.[0-9]+[a-z]*\"")
  string(REGEX REPLACE ".*\"([0-9]+.[0-9]+.[0-9]+[a-z]*)\".*" "\\1" qt_version_str "${QGLOBAL_H}")
  unset(QGLOBAL_H)

  # Under windows the qt library (MSVC) has the format qt-mtXYZ where XYZ is the
  # version X.Y.Z, so we need to remove the dots from version
  string(REGEX REPLACE "\\." "" qt_version_str_lib "${qt_version_str}")
  set(QT_VERSION_STRING "${qt_version_str}")
endif()

file(GLOB GLOB_PATHS_LIB /usr/lib/qt-3*/lib/)
if (QT_MT_REQUIRED)
  find_library(QT_QT_LIBRARY
    NAMES
    qt-mt qt-mt${qt_version_str_lib} qt-mtnc${qt_version_str_lib}
    qt-mtedu${qt_version_str_lib} qt-mt230nc qt-mtnc321 qt-mt3
    PATHS
      "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.1;InstallDir]"
      "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.0;InstallDir]"
      "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.1.0;InstallDir]"
      ENV QTDIR
      ${GLOB_PATHS_LIB}
      /usr/share/qt3
      C:/Progra~1/qt
    PATH_SUFFIXES
      lib lib/qt lib/qt3 qt qt3 qt/lib qt3/lib
    )

else ()
  find_library(QT_QT_LIBRARY
    NAMES
    qt qt-${qt_version_str_lib} qt-edu${qt_version_str_lib}
    qt-mt qt-mt${qt_version_str_lib} qt-mtnc${qt_version_str_lib}
    qt-mtedu${qt_version_str_lib} qt-mt230nc qt-mtnc321 qt-mt3
    PATHS
      "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.1;InstallDir]"
      "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.0;InstallDir]"
      "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.1.0;InstallDir]"
      ENV QTDIR
      ${GLOB_PATHS_LIB}
      /usr/share/qt3
      C:/Progra~1/qt/lib
    PATH_SUFFIXES
      lib lib/qt lib/qt3 qt qt3 qt/lib qt3/lib
    )
endif ()


find_library(QT_QASSISTANTCLIENT_LIBRARY
  NAMES qassistantclient
  PATHS
    "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.1;InstallDir]"
    "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.0;InstallDir]"
    "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.1.0;InstallDir]"
    ENV QTDIR
    ${GLOB_PATHS_LIB}
    /usr/share/qt3
    C:/Progra~1/qt
  PATH_SUFFIXES
    lib lib/qt lib/qt3 qt qt3 qt/lib qt3/lib
  )

# Qt 3 should prefer QTDIR over the PATH
find_program(QT_MOC_EXECUTABLE
  NAMES moc-qt3 moc3 moc3-mt moc
  HINTS
    ENV QTDIR
  PATHS
  "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.1;InstallDir]/include/Qt"
  "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.0;InstallDir]/include/Qt"
  "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.1.0;InstallDir]/include/Qt"
  ${GLOB_PATHS_BIN}
    /usr/share/qt3
    C:/Progra~1/qt
  PATH_SUFFIXES
    bin lib/qt lib/qt3 qt qt3 qt/bin qt3/bin lib/qt/bin lib/qt3/bin
  )

if(QT_MOC_EXECUTABLE)
  set ( QT_WRAP_CPP "YES")
endif()

# Qt 3 should prefer QTDIR over the PATH
find_program(QT_UIC_EXECUTABLE
  NAMES uic-qt3 uic3 uic3-mt uic
  HINTS
    ENV QTDIR
  PATHS
  "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.1;InstallDir]/include/Qt"
  "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.0;InstallDir]/include/Qt"
  "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.1.0;InstallDir]/include/Qt"
  ${GLOB_PATHS_BIN}
    /usr/share/qt3
    C:/Progra~1/qt
  PATH_SUFFIXES
    bin lib/qt lib/qt3 qt qt3 qt/bin qt3/bin lib/qt/bin lib/qt3/bin
  )

if(QT_UIC_EXECUTABLE)
  set ( QT_WRAP_UI "YES")
endif()

if (WIN32)
  find_library(QT_QTMAIN_LIBRARY qtmain
    HINTS
      ENV QTDIR
      "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.1;InstallDir]"
      "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.2.0;InstallDir]"
      "[HKEY_CURRENT_USER\\Software\\Trolltech\\Qt3Versions\\3.1.0;InstallDir]"
    PATHS
      "$ENV{ProgramFiles}/qt"
      "C:/Program Files/qt"
    PATH_SUFFIXES
      lib
    DOC "This Library is only needed by and included with Qt3 on MSWindows. It should be NOTFOUND, undefined or IGNORE otherwise."
    )
endif ()

#support old QT_MIN_VERSION if set, but not if version is supplied by find_package()
if(NOT Qt3_FIND_VERSION AND QT_MIN_VERSION)
  set(Qt3_FIND_VERSION ${QT_MIN_VERSION})
endif()

# if the include a library are found then we have it
include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
if (CMAKE_FIND_PACKAGE_NAME STREQUAL "Qt")
  # FindQt include()'s this module. It's an old pattern, but rather than trying
  # to suppress this from outside the module (which is then sensitive to the
  # contents, detect the case in this module and suppress it explicitly.
  set(FPHSA_NAME_MISMATCHED 1)
endif ()
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Qt3
                                  REQUIRED_VARS QT_QT_LIBRARY QT_INCLUDE_DIR QT_MOC_EXECUTABLE
                                  VERSION_VAR QT_VERSION_STRING)
unset(FPHSA_NAME_MISMATCHED)
set(QT_FOUND ${QT3_FOUND} )

if(QT_FOUND)
  set( QT_LIBRARIES ${QT_LIBRARIES} ${QT_QT_LIBRARY} )
  set( QT_DEFINITIONS "")

  if (WIN32 AND NOT CYGWIN)
    if (QT_QTMAIN_LIBRARY)
      # for version 3
      set (QT_DEFINITIONS -DQT_DLL -DQT_THREAD_SUPPORT -DNO_DEBUG)
      set (QT_LIBRARIES imm32.lib ${QT_QT_LIBRARY} ${QT_QTMAIN_LIBRARY} )
      set (QT_LIBRARIES ${QT_LIBRARIES} winmm wsock32)
    else ()
      # for version 2
      set (QT_LIBRARIES imm32.lib ws2_32.lib ${QT_QT_LIBRARY} )
    endif ()
  else ()
    set (QT_LIBRARIES ${QT_QT_LIBRARY} )

    set (QT_DEFINITIONS -DQT_SHARED -DQT_NO_DEBUG)
    if(QT_QT_LIBRARY MATCHES "qt-mt")
      set (QT_DEFINITIONS ${QT_DEFINITIONS} -DQT_THREAD_SUPPORT -D_REENTRANT)
    endif()

  endif ()

  if (QT_QASSISTANTCLIENT_LIBRARY)
    set (QT_LIBRARIES ${QT_QASSISTANTCLIENT_LIBRARY} ${QT_LIBRARIES})
  endif ()

  # Backwards compatibility for CMake1.4 and 1.2
  set (QT_MOC_EXE ${QT_MOC_EXECUTABLE} )
  set (QT_UIC_EXE ${QT_UIC_EXECUTABLE} )
  # for unix add X11 stuff
  if(UNIX)
    find_package(X11)
    if (X11_FOUND)
      set (QT_LIBRARIES ${QT_LIBRARIES} ${X11_LIBRARIES})
    endif ()
    if (CMAKE_DL_LIBS)
      set (QT_LIBRARIES ${QT_LIBRARIES} ${CMAKE_DL_LIBS})
    endif ()
  endif()
  if(QT_QT_LIBRARY MATCHES "qt-mt")
    find_package(Threads)
    set(QT_LIBRARIES ${QT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
  endif()
endif()

if(QT_MOC_EXECUTABLE)
  execute_process(COMMAND ${QT_MOC_EXECUTABLE} "-v"
                  OUTPUT_VARIABLE QTVERSION_MOC
                  ERROR_QUIET)
endif()
if(QT_UIC_EXECUTABLE)
  execute_process(COMMAND ${QT_UIC_EXECUTABLE} "-version"
                  OUTPUT_VARIABLE QTVERSION_UIC
                  ERROR_QUIET)
endif()

set(_QT_UIC_VERSION_3 FALSE)
if("${QTVERSION_UIC}" MATCHES " 3.")
  set(_QT_UIC_VERSION_3 TRUE)
endif()

set(_QT_MOC_VERSION_3 FALSE)
if("${QTVERSION_MOC}" MATCHES " 3.")
  set(_QT_MOC_VERSION_3 TRUE)
endif()

set(QT_WRAP_CPP FALSE)
if (QT_MOC_EXECUTABLE AND _QT_MOC_VERSION_3)
  set ( QT_WRAP_CPP TRUE)
endif ()

set(QT_WRAP_UI FALSE)
if (QT_UIC_EXECUTABLE AND _QT_UIC_VERSION_3)
  set ( QT_WRAP_UI TRUE)
endif ()

mark_as_advanced(
  QT_INCLUDE_DIR
  QT_QT_LIBRARY
  QT_QTMAIN_LIBRARY
  QT_QASSISTANTCLIENT_LIBRARY
  QT_UIC_EXECUTABLE
  QT_MOC_EXECUTABLE
  QT_WRAP_CPP
  QT_WRAP_UI
  )
