# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
FindQt4
-------

Finding and Using Qt4
^^^^^^^^^^^^^^^^^^^^^

This module can be used to find Qt4.  The most important issue is that
the Qt4 qmake is available via the system path.  This qmake is then
used to detect basically everything else.  This module defines a
number of :prop_tgt:`IMPORTED` targets, macros and variables.

Typical usage could be something like:

.. code-block:: cmake

   set(CMAKE_AUTOMOC ON)
   set(CMAKE_INCLUDE_CURRENT_DIR ON)
   find_package(Qt4 4.4.3 REQUIRED QtGui QtXml)
   add_executable(myexe main.cpp)
   target_link_libraries(myexe Qt4::QtGui Qt4::QtXml)

.. note::

 When using :prop_tgt:`IMPORTED` targets, the qtmain.lib static library is
 automatically linked on Windows for :prop_tgt:`WIN32 <WIN32_EXECUTABLE>`
 executables. To disable that globally, set the
 ``QT4_NO_LINK_QTMAIN`` variable before finding Qt4. To disable that
 for a particular executable, set the ``QT4_NO_LINK_QTMAIN`` target
 property to ``TRUE`` on the executable.

Qt Build Tools
^^^^^^^^^^^^^^

Qt relies on some bundled tools for code generation, such as ``moc`` for
meta-object code generation,``uic`` for widget layout and population,
and ``rcc`` for virtual filesystem content generation.  These tools may be
automatically invoked by :manual:`cmake(1)` if the appropriate conditions
are met.  See :manual:`cmake-qt(7)` for more.

Qt Macros
^^^^^^^^^

In some cases it can be necessary or useful to invoke the Qt build tools in a
more-manual way. Several macros are available to add targets for such uses.

::

  macro QT4_WRAP_CPP(outfiles inputfile ... [TARGET tgt] OPTIONS ...)
        create moc code from a list of files containing Qt class with
        the Q_OBJECT declaration.  Per-directory preprocessor definitions
        are also added.  If the <tgt> is specified, the
        INTERFACE_INCLUDE_DIRECTORIES and INTERFACE_COMPILE_DEFINITIONS from
        the <tgt> are passed to moc.  Options may be given to moc, such as
        those found when executing "moc -help".


::

  macro QT4_WRAP_UI(outfiles inputfile ... OPTIONS ...)
        create code from a list of Qt designer ui files.
        Options may be given to uic, such as those found
        when executing "uic -help"


::

  macro QT4_ADD_RESOURCES(outfiles inputfile ... OPTIONS ...)
        create code from a list of Qt resource files.
        Options may be given to rcc, such as those found
        when executing "rcc -help"


::

  macro QT4_GENERATE_MOC(inputfile outputfile [TARGET tgt])
        creates a rule to run moc on infile and create outfile.
        Use this if for some reason QT4_WRAP_CPP() isn't appropriate, e.g.
        because you need a custom filename for the moc file or something
        similar.  If the <tgt> is specified, the
        INTERFACE_INCLUDE_DIRECTORIES and INTERFACE_COMPILE_DEFINITIONS from
        the <tgt> are passed to moc.


::

  macro QT4_ADD_DBUS_INTERFACE(outfiles interface basename)
        Create the interface header and implementation files with the
        given basename from the given interface xml file and add it to
        the list of sources.

        You can pass additional parameters to the qdbusxml2cpp call by setting
        properties on the input file:

        INCLUDE the given file will be included in the generate interface header

        CLASSNAME the generated class is named accordingly

        NO_NAMESPACE the generated class is not wrapped in a namespace


::

  macro QT4_ADD_DBUS_INTERFACES(outfiles inputfile ... )
        Create the interface header and implementation files
        for all listed interface xml files.
        The basename will be automatically determined from the name
        of the xml file.

        The source file properties described for
        QT4_ADD_DBUS_INTERFACE also apply here.


::

  macro QT4_ADD_DBUS_ADAPTOR(outfiles xmlfile parentheader parentclassname
                             [basename] [classname])
        create a dbus adaptor (header and implementation file) from the xml file
        describing the interface, and add it to the list of sources. The adaptor
        forwards the calls to a parent class, defined in parentheader and named
        parentclassname. The name of the generated files will be
        <basename>adaptor.{cpp,h} where basename defaults to the basename of the
        xml file.
        If <classname> is provided, then it will be used as the classname of the
        adaptor itself.


::

  macro QT4_GENERATE_DBUS_INTERFACE( header [interfacename] OPTIONS ...)
        generate the xml interface file from the given header.
        If the optional argument interfacename is omitted, the name of the
        interface file is constructed from the basename of the header with
        the suffix .xml appended.
        Options may be given to qdbuscpp2xml, such as those found when
        executing "qdbuscpp2xml --help"


::

  macro QT4_CREATE_TRANSLATION( qm_files directories ... sources ...
                                ts_files ... OPTIONS ...)
        out: qm_files
        in:  directories sources ts_files
        options: flags to pass to lupdate, such as -extensions to specify
        extensions for a directory scan.
        generates commands to create .ts (via lupdate) and .qm
        (via lrelease) - files from directories and/or sources. The ts files are
        created and/or updated in the source tree (unless given with full paths).
        The qm files are generated in the build tree.
        Updating the translations can be done by adding the qm_files
        to the source list of your library/executable, so they are
        always updated, or by adding a custom target to control when
        they get updated/generated.


::

  macro QT4_ADD_TRANSLATION( qm_files ts_files ... )
        out: qm_files
        in:  ts_files
        generates commands to create .qm from .ts - files. The generated
        filenames can be found in qm_files. The ts_files
        must exist and are not updated in any way.


::

  macro QT4_AUTOMOC(sourcefile1 sourcefile2 ... [TARGET tgt])
        The qt4_automoc macro is obsolete.  Use the CMAKE_AUTOMOC feature instead.
        This macro is still experimental.
        It can be used to have moc automatically handled.
        So if you have the files foo.h and foo.cpp, and in foo.h a
        a class uses the Q_OBJECT macro, moc has to run on it. If you don't
        want to use QT4_WRAP_CPP() (which is reliable and mature), you can insert
        #include "foo.moc"
        in foo.cpp and then give foo.cpp as argument to QT4_AUTOMOC(). This will
        scan all listed files at cmake-time for such included moc files and if it
        finds them cause a rule to be generated to run moc at build time on the
        accompanying header file foo.h.
        If a source file has the SKIP_AUTOMOC property set it will be ignored by
        this macro.
        If the <tgt> is specified, the INTERFACE_INCLUDE_DIRECTORIES and
        INTERFACE_COMPILE_DEFINITIONS from the <tgt> are passed to moc.


::

 function QT4_USE_MODULES( target [link_type] modules...)
        This function is obsolete. Use target_link_libraries with IMPORTED targets
        instead.
        Make <target> use the <modules> from Qt. Using a Qt module means
        to link to the library, add the relevant include directories for the
        module, and add the relevant compiler defines for using the module.
        Modules are roughly equivalent to components of Qt4, so usage would be
        something like:
         qt4_use_modules(myexe Core Gui Declarative)
        to use QtCore, QtGui and QtDeclarative. The optional <link_type> argument
        can be specified as either LINK_PUBLIC or LINK_PRIVATE to specify the
        same argument to the target_link_libraries call.


IMPORTED Targets
^^^^^^^^^^^^^^^^

A particular Qt library may be used by using the corresponding
:prop_tgt:`IMPORTED` target with the :command:`target_link_libraries`
command:

.. code-block:: cmake

  target_link_libraries(myexe Qt4::QtGui Qt4::QtXml)

Using a target in this way causes :cmake(1)` to use the appropriate include
directories and compile definitions for the target when compiling ``myexe``.

Targets are aware of their dependencies, so for example it is not necessary
to list ``Qt4::QtCore`` if another Qt library is listed, and it is not
necessary to list ``Qt4::QtGui`` if ``Qt4::QtDeclarative`` is listed.
Targets may be tested for existence in the usual way with the
:command:`if(TARGET)` command.

The Qt toolkit may contain both debug and release libraries.
:manual:`cmake(1)` will choose the appropriate version based on the build
configuration.

``Qt4::QtCore``
 The QtCore target
``Qt4::QtGui``
 The QtGui target
``Qt4::Qt3Support``
 The Qt3Support target
``Qt4::QtAssistant``
 The QtAssistant target
``Qt4::QtAssistantClient``
 The QtAssistantClient target
``Qt4::QAxContainer``
 The QAxContainer target (Windows only)
``Qt4::QAxServer``
 The QAxServer target (Windows only)
``Qt4::QtDBus``
 The QtDBus target
``Qt4::QtDeclarative``
 The QtDeclarative target
``Qt4::QtDesigner``
 The QtDesigner target
``Qt4::QtDesignerComponents``
 The QtDesignerComponents target
``Qt4::QtHelp``
 The QtHelp target
``Qt4::QtMotif``
 The QtMotif target
``Qt4::QtMultimedia``
 The QtMultimedia target
``Qt4::QtNetwork``
 The QtNetwork target
``Qt4::QtNsPLugin``
 The QtNsPLugin target
``Qt4::QtOpenGL``
 The QtOpenGL target
``Qt4::QtScript``
 The QtScript target
``Qt4::QtScriptTools``
 The QtScriptTools target
``Qt4::QtSql``
 The QtSql target
``Qt4::QtSvg``
 The QtSvg target
``Qt4::QtTest``
 The QtTest target
``Qt4::QtUiTools``
 The QtUiTools target
``Qt4::QtWebKit``
 The QtWebKit target
``Qt4::QtXml``
 The QtXml target
``Qt4::QtXmlPatterns``
 The QtXmlPatterns target
``Qt4::phonon``
 The phonon target

Result Variables
^^^^^^^^^^^^^^^^

  Below is a detailed list of variables that FindQt4.cmake sets.

``Qt4_FOUND``
 If false, don't try to use Qt 4.
``QT_FOUND``
 If false, don't try to use Qt. This variable is for compatibility only.
``QT4_FOUND``
 If false, don't try to use Qt 4. This variable is for compatibility only.
``QT_VERSION_MAJOR``
 The major version of Qt found.
``QT_VERSION_MINOR``
 The minor version of Qt found.
``QT_VERSION_PATCH``
 The patch version of Qt found.
#]=======================================================================]

# Use find_package( Qt4 COMPONENTS ... ) to enable modules
if( Qt4_FIND_COMPONENTS )
  foreach( component ${Qt4_FIND_COMPONENTS} )
    string( TOUPPER ${component} _COMPONENT )
    set( QT_USE_${_COMPONENT} 1 )
  endforeach()

  # To make sure we don't use QtCore or QtGui when not in COMPONENTS
  if(NOT QT_USE_QTCORE)
    set( QT_DONT_USE_QTCORE 1 )
  endif()

  if(NOT QT_USE_QTGUI)
    set( QT_DONT_USE_QTGUI 1 )
  endif()

endif()

# If Qt3 has already been found, fail.
if(QT_QT_LIBRARY)
  if(Qt4_FIND_REQUIRED)
    message( FATAL_ERROR "Qt3 and Qt4 cannot be used together in one project.  If switching to Qt4, the CMakeCache.txt needs to be cleaned.")
  else()
    if(NOT Qt4_FIND_QUIETLY)
      message( STATUS    "Qt3 and Qt4 cannot be used together in one project.  If switching to Qt4, the CMakeCache.txt needs to be cleaned.")
    endif()
    return()
  endif()
endif()


include(${CMAKE_CURRENT_LIST_DIR}/CheckCXXSymbolExists.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/MacroAddFileDependencies.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/CMakePushCheckState.cmake)

set(QT_USE_FILE ${CMAKE_ROOT}/Modules/UseQt4.cmake)

set( QT_DEFINITIONS "")

# convenience macro for dealing with debug/release library names
macro (_QT4_ADJUST_LIB_VARS _camelCaseBasename)

  string(TOUPPER "${_camelCaseBasename}" basename)

  # The name of the imported targets, i.e. the prefix "Qt4::" must not change,
  # since it is stored in EXPORT-files as name of a required library. If the name would change
  # here, this would lead to the imported Qt4-library targets not being resolved by cmake anymore.
  if (QT_${basename}_LIBRARY_RELEASE OR QT_${basename}_LIBRARY_DEBUG)

    if(NOT TARGET Qt4::${_camelCaseBasename})
      add_library(Qt4::${_camelCaseBasename} UNKNOWN IMPORTED )

      if (QT_${basename}_LIBRARY_RELEASE)
        set_property(TARGET Qt4::${_camelCaseBasename} APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
        set(_location "${QT_${basename}_LIBRARY_RELEASE}")
        if(QT_USE_FRAMEWORKS AND EXISTS ${_location}/${_camelCaseBasename})
          set_property(TARGET Qt4::${_camelCaseBasename}        PROPERTY IMPORTED_LOCATION_RELEASE "${_location}/${_camelCaseBasename}" )
        else()
          set_property(TARGET Qt4::${_camelCaseBasename}        PROPERTY IMPORTED_LOCATION_RELEASE "${_location}" )
        endif()
      endif ()

      if (QT_${basename}_LIBRARY_DEBUG)
        set_property(TARGET Qt4::${_camelCaseBasename} APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
        set(_location "${QT_${basename}_LIBRARY_DEBUG}")
        if(QT_USE_FRAMEWORKS AND EXISTS ${_location}/${_camelCaseBasename})
          set_property(TARGET Qt4::${_camelCaseBasename}        PROPERTY IMPORTED_LOCATION_DEBUG "${_location}/${_camelCaseBasename}" )
        else()
          set_property(TARGET Qt4::${_camelCaseBasename}        PROPERTY IMPORTED_LOCATION_DEBUG "${_location}" )
        endif()
      endif ()
      set_property(TARGET Qt4::${_camelCaseBasename} PROPERTY
        INTERFACE_INCLUDE_DIRECTORIES
          "${QT_${basename}_INCLUDE_DIR}"
      )
      string(REGEX REPLACE "^QT" "" _stemname ${basename})
      set_property(TARGET Qt4::${_camelCaseBasename} PROPERTY
        INTERFACE_COMPILE_DEFINITIONS
          "QT_${_stemname}_LIB"
      )
    endif()

    # If QT_USE_IMPORTED_TARGETS is enabled, the QT_QTFOO_LIBRARY variables are set to point at these
    # imported targets. This works better in general, and is also in almost all cases fully
    # backward compatible. The only issue is when a project A which had this enabled then exports its
    # libraries via export or export_library_dependencies(). In this case the libraries from project
    # A will depend on the imported Qt targets, and the names of these imported targets will be stored
    # in the dependency files on disk. This means when a project B then uses project A, these imported
    # targets must be created again, otherwise e.g. "Qt4__QtCore" will be interpreted as name of a
    # library file on disk, and not as a target, and linking will fail:
    if(QT_USE_IMPORTED_TARGETS)
        set(QT_${basename}_LIBRARY       Qt4::${_camelCaseBasename} )
        set(QT_${basename}_LIBRARIES     Qt4::${_camelCaseBasename} )
    else()

      # if the release- as well as the debug-version of the library have been found:
      if (QT_${basename}_LIBRARY_DEBUG AND QT_${basename}_LIBRARY_RELEASE)
        # if the generator is multi-config or if CMAKE_BUILD_TYPE is set for
        # single-config generators, set optimized and debug libraries
        get_property(_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(_isMultiConfig OR CMAKE_BUILD_TYPE)
          set(QT_${basename}_LIBRARY       optimized ${QT_${basename}_LIBRARY_RELEASE} debug ${QT_${basename}_LIBRARY_DEBUG})
        else()
          # For single-config generators where CMAKE_BUILD_TYPE has no value,
          # just use the release libraries
          set(QT_${basename}_LIBRARY       ${QT_${basename}_LIBRARY_RELEASE} )
        endif()
        set(QT_${basename}_LIBRARIES       optimized ${QT_${basename}_LIBRARY_RELEASE} debug ${QT_${basename}_LIBRARY_DEBUG})
      endif ()

      # if only the release version was found, set the debug variable also to the release version
      if (QT_${basename}_LIBRARY_RELEASE AND NOT QT_${basename}_LIBRARY_DEBUG)
        set(QT_${basename}_LIBRARY_DEBUG ${QT_${basename}_LIBRARY_RELEASE})
        set(QT_${basename}_LIBRARY       ${QT_${basename}_LIBRARY_RELEASE})
        set(QT_${basename}_LIBRARIES     ${QT_${basename}_LIBRARY_RELEASE})
      endif ()

      # if only the debug version was found, set the release variable also to the debug version
      if (QT_${basename}_LIBRARY_DEBUG AND NOT QT_${basename}_LIBRARY_RELEASE)
        set(QT_${basename}_LIBRARY_RELEASE ${QT_${basename}_LIBRARY_DEBUG})
        set(QT_${basename}_LIBRARY         ${QT_${basename}_LIBRARY_DEBUG})
        set(QT_${basename}_LIBRARIES       ${QT_${basename}_LIBRARY_DEBUG})
      endif ()

      # put the value in the cache:
      set(QT_${basename}_LIBRARY ${QT_${basename}_LIBRARY} CACHE STRING "The Qt ${basename} library" FORCE)

    endif()

    set(QT_${basename}_FOUND 1)

  else ()

    set(QT_${basename}_LIBRARY "" CACHE STRING "The Qt ${basename} library" FORCE)

  endif ()

  if (QT_${basename}_INCLUDE_DIR)
    #add the include directory to QT_INCLUDES
    set(QT_INCLUDES "${QT_${basename}_INCLUDE_DIR}" ${QT_INCLUDES})
  endif ()

  # Make variables changeable to the advanced user
  mark_as_advanced(QT_${basename}_LIBRARY QT_${basename}_LIBRARY_RELEASE QT_${basename}_LIBRARY_DEBUG QT_${basename}_INCLUDE_DIR)
endmacro ()

function(_QT4_QUERY_QMAKE VAR RESULT)
  execute_process(COMMAND "${QT_QMAKE_EXECUTABLE}" -query ${VAR}
    RESULT_VARIABLE return_code
    OUTPUT_VARIABLE output
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT return_code)
    file(TO_CMAKE_PATH "${output}" output)
    set(${RESULT} ${output} PARENT_SCOPE)
  endif()
endfunction()

function(_QT4_GET_VERSION_COMPONENTS VERSION RESULT_MAJOR RESULT_MINOR RESULT_PATCH)
  string(REGEX REPLACE "^([0-9]+)\\.[0-9]+\\.[0-9]+.*" "\\1" QT_VERSION_MAJOR "${QTVERSION}")
  string(REGEX REPLACE "^[0-9]+\\.([0-9]+)\\.[0-9]+.*" "\\1" QT_VERSION_MINOR "${QTVERSION}")
  string(REGEX REPLACE "^[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" QT_VERSION_PATCH "${QTVERSION}")

  set(${RESULT_MAJOR} ${QT_VERSION_MAJOR} PARENT_SCOPE)
  set(${RESULT_MINOR} ${QT_VERSION_MINOR} PARENT_SCOPE)
  set(${RESULT_PATCH} ${QT_VERSION_PATCH} PARENT_SCOPE)
endfunction()

function(_QT4_FIND_QMAKE QMAKE_NAMES QMAKE_RESULT VERSION_RESULT)
  list(LENGTH QMAKE_NAMES QMAKE_NAMES_LEN)
  if(${QMAKE_NAMES_LEN} EQUAL 0)
    return()
  endif()
  list(GET QMAKE_NAMES 0 QMAKE_NAME)

  get_filename_component(qt_install_version "[HKEY_CURRENT_USER\\Software\\trolltech\\Versions;DefaultQtVersion]" NAME)

  find_program(QT_QMAKE_EXECUTABLE NAMES ${QMAKE_NAME}
    PATHS
      ENV QTDIR
      "[HKEY_CURRENT_USER\\Software\\Trolltech\\Versions\\${qt_install_version};InstallDir]"
    PATH_SUFFIXES bin
    DOC "The qmake executable for the Qt installation to use"
  )

  set(major 0)
  if (QT_QMAKE_EXECUTABLE)
    _qt4_query_qmake(QT_VERSION QTVERSION)
    _qt4_get_version_components("${QTVERSION}" major minor patch)
  endif()

  if (NOT QT_QMAKE_EXECUTABLE OR NOT "${major}" EQUAL 4)
    set(curr_qmake "${QT_QMAKE_EXECUTABLE}")
    set(curr_qt_version "${QTVERSION}")

    set(QT_QMAKE_EXECUTABLE NOTFOUND CACHE FILEPATH "" FORCE)
    list(REMOVE_AT QMAKE_NAMES 0)
    _qt4_find_qmake("${QMAKE_NAMES}" QMAKE QTVERSION)

    _qt4_get_version_components("${QTVERSION}" major minor patch)
    if (NOT ${major} EQUAL 4)
      # Restore possibly found qmake and it's version; these are used later
      # in error message if incorrect version is found
      set(QT_QMAKE_EXECUTABLE "${curr_qmake}" CACHE FILEPATH "" FORCE)
      set(QTVERSION "${curr_qt_version}")
    endif()

  endif()


  set(${QMAKE_RESULT} "${QT_QMAKE_EXECUTABLE}" PARENT_SCOPE)
  set(${VERSION_RESULT} "${QTVERSION}" PARENT_SCOPE)
endfunction()


set(QT4_INSTALLED_VERSION_TOO_OLD FALSE)

set(_QT4_QMAKE_NAMES qmake qmake4 qmake-qt4 qmake-mac)
_qt4_find_qmake("${_QT4_QMAKE_NAMES}" QT_QMAKE_EXECUTABLE QTVERSION)

if (QT_QMAKE_EXECUTABLE AND
  QTVERSION VERSION_GREATER 3 AND QTVERSION VERSION_LESS 5)

  if (Qt5Core_FOUND)
    # Qt5CoreConfig sets QT_MOC_EXECUTABLE as a non-cache variable to the Qt 5
    # path to moc.  Unset that variable when Qt 4 and 5 are used together, so
    # that when find_program looks for moc, it is not set to the Qt 5 version.
    # If FindQt4 has already put the Qt 4 path in the cache, the unset()
    # command 'unhides' the (correct) cache variable.
    unset(QT_MOC_EXECUTABLE)
  endif()
  if (QT_QMAKE_EXECUTABLE_LAST)
    string(COMPARE NOTEQUAL "${QT_QMAKE_EXECUTABLE_LAST}" "${QT_QMAKE_EXECUTABLE}" QT_QMAKE_CHANGED)
  endif()
  set(QT_QMAKE_EXECUTABLE_LAST "${QT_QMAKE_EXECUTABLE}" CACHE INTERNAL "" FORCE)

  _qt4_get_version_components("${QTVERSION}" QT_VERSION_MAJOR QT_VERSION_MINOR QT_VERSION_PATCH)

  # ask qmake for the mkspecs directory
  # we do this first because QT_LIBINFIX might be set
  if (NOT QT_MKSPECS_DIR  OR  QT_QMAKE_CHANGED)
    _qt4_query_qmake(QMAKE_MKSPECS qt_mkspecs_dirs)
    # do not replace : on windows as it might be a drive letter
    # and windows should already use ; as a separator
    if(NOT WIN32)
      string(REPLACE ":" ";" qt_mkspecs_dirs "${qt_mkspecs_dirs}")
    endif()

    find_path(QT_MKSPECS_DIR NAMES qconfig.pri
      HINTS ${qt_mkspecs_dirs}
      PATH_SUFFIXES mkspecs share/qt4/mkspecs
      DOC "The location of the Qt mkspecs containing qconfig.pri")
  endif()

  if(EXISTS "${QT_MKSPECS_DIR}/qconfig.pri")
    file(READ ${QT_MKSPECS_DIR}/qconfig.pri _qconfig_FILE_contents)
    string(REGEX MATCH "QT_CONFIG[^\n]+" QT_QCONFIG "${_qconfig_FILE_contents}")
    string(REGEX MATCH "CONFIG[^\n]+" QT_CONFIG "${_qconfig_FILE_contents}")
    string(REGEX MATCH "EDITION[^\n]+" QT_EDITION "${_qconfig_FILE_contents}")
    string(REGEX MATCH "QT_LIBINFIX[^\n]+" _qconfig_qt_libinfix "${_qconfig_FILE_contents}")
    string(REGEX REPLACE "QT_LIBINFIX *= *([^\n]*)" "\\1" QT_LIBINFIX "${_qconfig_qt_libinfix}")
  endif()
  if("${QT_EDITION}" MATCHES "DesktopLight")
    set(QT_EDITION_DESKTOPLIGHT 1)
  endif()

  # ask qmake for the library dir as a hint, then search for QtCore library and use that as a reference for finding the
  # others and for setting QT_LIBRARY_DIR
  if (NOT (QT_QTCORE_LIBRARY_RELEASE OR QT_QTCORE_LIBRARY_DEBUG)  OR QT_QMAKE_CHANGED)
    _qt4_query_qmake(QT_INSTALL_LIBS QT_LIBRARY_DIR_TMP)
    set(QT_QTCORE_LIBRARY_RELEASE NOTFOUND)
    set(QT_QTCORE_LIBRARY_DEBUG NOTFOUND)
    find_library(QT_QTCORE_LIBRARY_RELEASE
                 NAMES QtCore${QT_LIBINFIX} QtCore${QT_LIBINFIX}4
                 HINTS ${QT_LIBRARY_DIR_TMP}
                 NO_DEFAULT_PATH
        )
    find_library(QT_QTCORE_LIBRARY_DEBUG
                 NAMES QtCore${QT_LIBINFIX}_debug QtCore${QT_LIBINFIX}d QtCore${QT_LIBINFIX}d4
                 HINTS ${QT_LIBRARY_DIR_TMP}
                 NO_DEFAULT_PATH
        )

    if(NOT QT_QTCORE_LIBRARY_RELEASE AND NOT QT_QTCORE_LIBRARY_DEBUG)
      find_library(QT_QTCORE_LIBRARY_RELEASE
                   NAMES QtCore${QT_LIBINFIX} QtCore${QT_LIBINFIX}4
                   HINTS ${QT_LIBRARY_DIR_TMP}
          )
      find_library(QT_QTCORE_LIBRARY_DEBUG
                   NAMES QtCore${QT_LIBINFIX}_debug QtCore${QT_LIBINFIX}d QtCore${QT_LIBINFIX}d4
                   HINTS ${QT_LIBRARY_DIR_TMP}
          )
    endif()

    # try dropping a hint if trying to use Visual Studio with Qt built by MinGW
    if(NOT QT_QTCORE_LIBRARY_RELEASE AND MSVC)
      if(EXISTS ${QT_LIBRARY_DIR_TMP}/libqtmain.a)
        message( FATAL_ERROR "It appears you're trying to use Visual Studio with Qt built by MinGW.  Those compilers do not produce code compatible with each other.")
      endif()
    endif()

  endif ()

  # set QT_LIBRARY_DIR based on location of QtCore found.
  if(QT_QTCORE_LIBRARY_RELEASE)
    get_filename_component(QT_LIBRARY_DIR_TMP "${QT_QTCORE_LIBRARY_RELEASE}" PATH)
    set(QT_LIBRARY_DIR ${QT_LIBRARY_DIR_TMP} CACHE INTERNAL "Qt library dir" FORCE)
    set(QT_QTCORE_FOUND 1)
  elseif(QT_QTCORE_LIBRARY_DEBUG)
    get_filename_component(QT_LIBRARY_DIR_TMP "${QT_QTCORE_LIBRARY_DEBUG}" PATH)
    set(QT_LIBRARY_DIR ${QT_LIBRARY_DIR_TMP} CACHE INTERNAL "Qt library dir" FORCE)
    set(QT_QTCORE_FOUND 1)
  else()
    if(NOT Qt4_FIND_QUIETLY)
      message(WARNING
        "${QT_QMAKE_EXECUTABLE} reported QT_INSTALL_LIBS as "
        "\"${QT_LIBRARY_DIR_TMP}\" "
        "but QtCore could not be found there.  "
        "Qt is NOT installed correctly for the target build environment.")
    endif()
    set(Qt4_FOUND FALSE)
    if(Qt4_FIND_REQUIRED)
      message( FATAL_ERROR "Could NOT find QtCore. Check ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log for more details.")
    else()
      return()
    endif()
  endif()

  # ask qmake for the binary dir
  if (NOT QT_BINARY_DIR  OR  QT_QMAKE_CHANGED)
    _qt4_query_qmake(QT_INSTALL_BINS qt_bins)
    set(QT_BINARY_DIR ${qt_bins} CACHE INTERNAL "" FORCE)
  endif ()

  if (APPLE)
    set(CMAKE_FIND_FRAMEWORK_OLD ${CMAKE_FIND_FRAMEWORK})
    if (EXISTS ${QT_LIBRARY_DIR}/QtCore.framework)
      set(QT_USE_FRAMEWORKS ON CACHE INTERNAL "" FORCE)
      set(CMAKE_FIND_FRAMEWORK FIRST)
    else ()
      set(QT_USE_FRAMEWORKS OFF CACHE INTERNAL "" FORCE)
      set(CMAKE_FIND_FRAMEWORK LAST)
    endif ()
  endif ()

  # ask qmake for the include dir
  if (QT_LIBRARY_DIR AND (NOT QT_QTCORE_INCLUDE_DIR OR NOT QT_HEADERS_DIR OR  QT_QMAKE_CHANGED))
      _qt4_query_qmake(QT_INSTALL_HEADERS qt_headers)
      set(QT_QTCORE_INCLUDE_DIR NOTFOUND)
      find_path(QT_QTCORE_INCLUDE_DIR QtCore
                HINTS ${qt_headers} ${QT_LIBRARY_DIR}
                PATH_SUFFIXES QtCore qt4/QtCore
                NO_DEFAULT_PATH
        )
      if(NOT QT_QTCORE_INCLUDE_DIR)
        find_path(QT_QTCORE_INCLUDE_DIR QtCore
                  HINTS ${qt_headers} ${QT_LIBRARY_DIR}
                  PATH_SUFFIXES QtCore qt4/QtCore
          )
      endif()

      # Set QT_HEADERS_DIR based on finding QtCore header
      if(QT_QTCORE_INCLUDE_DIR)
        if(QT_USE_FRAMEWORKS)
          set(QT_HEADERS_DIR "${qt_headers}" CACHE INTERNAL "" FORCE)
        else()
          get_filename_component(qt_headers "${QT_QTCORE_INCLUDE_DIR}/../" ABSOLUTE)
          set(QT_HEADERS_DIR "${qt_headers}" CACHE INTERNAL "" FORCE)
        endif()
      else()
        message("Warning: QT_QMAKE_EXECUTABLE reported QT_INSTALL_HEADERS as ${qt_headers}")
        message("Warning: But QtCore couldn't be found.  Qt must NOT be installed correctly.")
      endif()
  endif()

  if(APPLE)
    set(CMAKE_FIND_FRAMEWORK ${CMAKE_FIND_FRAMEWORK_OLD})
  endif()

  # Set QT_INCLUDE_DIR based on QT_HEADERS_DIR
  if(QT_HEADERS_DIR)
    if(QT_USE_FRAMEWORKS)
      # Qt/Mac frameworks has two include dirs.
      # One is the framework include for which CMake will add a -F flag
      # and the other is an include dir for non-framework Qt modules
      set(QT_INCLUDE_DIR ${QT_HEADERS_DIR} ${QT_QTCORE_LIBRARY_RELEASE} )
    else()
      set(QT_INCLUDE_DIR ${QT_HEADERS_DIR})
    endif()
  endif()

  # Set QT_INCLUDES
  set( QT_INCLUDES ${QT_MKSPECS_DIR}/default ${QT_INCLUDE_DIR} ${QT_QTCORE_INCLUDE_DIR})


  # ask qmake for the documentation directory
  if (QT_LIBRARY_DIR AND NOT QT_DOC_DIR  OR  QT_QMAKE_CHANGED)
    _qt4_query_qmake(QT_INSTALL_DOCS qt_doc_dir)
    set(QT_DOC_DIR ${qt_doc_dir} CACHE PATH "The location of the Qt docs" FORCE)
  endif ()


  # ask qmake for the plugins directory
  if (QT_LIBRARY_DIR AND NOT QT_PLUGINS_DIR  OR  QT_QMAKE_CHANGED)
    _qt4_query_qmake(QT_INSTALL_PLUGINS qt_plugins_dir)
    if(CMAKE_CROSSCOMPILING OR NOT qt_plugins_dir)
      find_path(QT_PLUGINS_DIR
        NAMES accessible bearer codecs designer graphicssystems iconengines imageformats inputmethods qmltooling script sqldrivers
        HINTS ${qt_plugins_dir}
        PATH_SUFFIXES plugins lib/qt4/plugins
        DOC "The location of the Qt plugins")
    else()
      set(QT_PLUGINS_DIR ${qt_plugins_dir} CACHE PATH "The location of the Qt plugins")
    endif()
  endif ()

  # ask qmake for the translations directory
  if (QT_LIBRARY_DIR AND NOT QT_TRANSLATIONS_DIR  OR  QT_QMAKE_CHANGED)
    _qt4_query_qmake(QT_INSTALL_TRANSLATIONS qt_translations_dir)
    set(QT_TRANSLATIONS_DIR ${qt_translations_dir} CACHE PATH "The location of the Qt translations" FORCE)
  endif ()

  # ask qmake for the imports directory
  if (QT_LIBRARY_DIR AND NOT QT_IMPORTS_DIR OR QT_QMAKE_CHANGED)
    _qt4_query_qmake(QT_INSTALL_IMPORTS qt_imports_dir)
    if(CMAKE_CROSSCOMPILING OR NOT qt_imports_dir)
      find_path(QT_IMPORTS_DIR NAMES Qt
        HINTS ${qt_imports_dir}
        PATH_SUFFIXES imports lib/qt4/imports
        DOC "The location of the Qt imports")
    else()
      set(QT_IMPORTS_DIR ${qt_imports_dir} CACHE PATH "The location of the Qt imports")
    endif()
  endif ()

  # Make variables changeable to the advanced user
  mark_as_advanced( QT_LIBRARY_DIR QT_DOC_DIR QT_MKSPECS_DIR
                    QT_PLUGINS_DIR QT_TRANSLATIONS_DIR)




  #############################################
  #
  # Find out what window system we're using
  #
  #############################################
  cmake_push_check_state()
  # Add QT_INCLUDE_DIR to CMAKE_REQUIRED_INCLUDES
  list(APPEND CMAKE_REQUIRED_INCLUDES "${QT_INCLUDE_DIR}")
  set(CMAKE_REQUIRED_QUIET ${Qt4_FIND_QUIETLY})
  # Check for Window system symbols (note: only one should end up being set)
  CHECK_CXX_SYMBOL_EXISTS(Q_WS_X11 "QtCore/qglobal.h" Q_WS_X11)
  CHECK_CXX_SYMBOL_EXISTS(Q_WS_WIN "QtCore/qglobal.h" Q_WS_WIN)
  CHECK_CXX_SYMBOL_EXISTS(Q_WS_QWS "QtCore/qglobal.h" Q_WS_QWS)
  CHECK_CXX_SYMBOL_EXISTS(Q_WS_MAC "QtCore/qglobal.h" Q_WS_MAC)
  if(Q_WS_MAC)
    if(QT_QMAKE_CHANGED)
      unset(QT_MAC_USE_COCOA CACHE)
    endif()
    CHECK_CXX_SYMBOL_EXISTS(QT_MAC_USE_COCOA "QtCore/qconfig.h" QT_MAC_USE_COCOA)
  endif()

  if (QT_QTCOPY_REQUIRED)
     CHECK_CXX_SYMBOL_EXISTS(QT_IS_QTCOPY "QtCore/qglobal.h" QT_KDE_QT_COPY)
     if (NOT QT_IS_QTCOPY)
        message(FATAL_ERROR "qt-copy is required, but hasn't been found")
     endif ()
  endif ()

  cmake_pop_check_state()
  #
  #############################################



  ########################################
  #
  #       Setting the INCLUDE-Variables
  #
  ########################################

  set(QT_MODULES QtGui Qt3Support QtSvg QtScript QtTest QtUiTools
                 QtHelp QtWebKit QtXmlPatterns phonon QtNetwork QtMultimedia
                 QtNsPlugin QtOpenGL QtSql QtXml QtDesigner QtDBus QtScriptTools
                 QtDeclarative)

  if(Q_WS_X11)
    set(QT_MODULES ${QT_MODULES} QtMotif)
  endif()

  if(QT_QMAKE_CHANGED)
    foreach(QT_MODULE ${QT_MODULES})
      string(TOUPPER ${QT_MODULE} _upper_qt_module)
      set(QT_${_upper_qt_module}_INCLUDE_DIR NOTFOUND)
      set(QT_${_upper_qt_module}_LIBRARY_RELEASE NOTFOUND)
      set(QT_${_upper_qt_module}_LIBRARY_DEBUG NOTFOUND)
    endforeach()
    set(QT_QTDESIGNERCOMPONENTS_INCLUDE_DIR NOTFOUND)
    set(QT_QTDESIGNERCOMPONENTS_LIBRARY_RELEASE NOTFOUND)
    set(QT_QTDESIGNERCOMPONENTS_LIBRARY_DEBUG NOTFOUND)
    set(QT_QTASSISTANTCLIENT_INCLUDE_DIR NOTFOUND)
    set(QT_QTASSISTANTCLIENT_LIBRARY_RELEASE NOTFOUND)
    set(QT_QTASSISTANTCLIENT_LIBRARY_DEBUG NOTFOUND)
    set(QT_QTASSISTANT_INCLUDE_DIR NOTFOUND)
    set(QT_QTASSISTANT_LIBRARY_RELEASE NOTFOUND)
    set(QT_QTASSISTANT_LIBRARY_DEBUG NOTFOUND)
    set(QT_QTCLUCENE_LIBRARY_RELEASE NOTFOUND)
    set(QT_QTCLUCENE_LIBRARY_DEBUG NOTFOUND)
    set(QT_QAXCONTAINER_INCLUDE_DIR NOTFOUND)
    set(QT_QAXCONTAINER_LIBRARY_RELEASE NOTFOUND)
    set(QT_QAXCONTAINER_LIBRARY_DEBUG NOTFOUND)
    set(QT_QAXSERVER_INCLUDE_DIR NOTFOUND)
    set(QT_QAXSERVER_LIBRARY_RELEASE NOTFOUND)
    set(QT_QAXSERVER_LIBRARY_DEBUG NOTFOUND)
    if(Q_WS_WIN)
      set(QT_QTMAIN_LIBRARY_DEBUG NOTFOUND)
      set(QT_QTMAIN_LIBRARY_RELEASE NOTFOUND)
    endif()
  endif()

  foreach(QT_MODULE ${QT_MODULES})
    string(TOUPPER ${QT_MODULE} _upper_qt_module)
    find_path(QT_${_upper_qt_module}_INCLUDE_DIR ${QT_MODULE}
              PATHS
              ${QT_HEADERS_DIR}/${QT_MODULE}
              ${QT_LIBRARY_DIR}/${QT_MODULE}.framework/Headers
              NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
      )
    # phonon doesn't seem consistent, let's try phonondefs.h for some
    # installations
    if(${QT_MODULE} STREQUAL "phonon")
      find_path(QT_${_upper_qt_module}_INCLUDE_DIR phonondefs.h
                PATHS
                ${QT_HEADERS_DIR}/${QT_MODULE}
                ${QT_LIBRARY_DIR}/${QT_MODULE}.framework/Headers
                NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
        )
    endif()
  endforeach()

  if(Q_WS_WIN)
    set(QT_MODULES ${QT_MODULES} QAxContainer QAxServer)
    # Set QT_AXCONTAINER_INCLUDE_DIR and QT_AXSERVER_INCLUDE_DIR
    find_path(QT_QAXCONTAINER_INCLUDE_DIR ActiveQt
      PATHS ${QT_HEADERS_DIR}/ActiveQt
      NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
      )
    find_path(QT_QAXSERVER_INCLUDE_DIR ActiveQt
      PATHS ${QT_HEADERS_DIR}/ActiveQt
      NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
      )
  endif()

  # Set QT_QTDESIGNERCOMPONENTS_INCLUDE_DIR
  find_path(QT_QTDESIGNERCOMPONENTS_INCLUDE_DIR QDesignerComponents
    PATHS
    ${QT_HEADERS_DIR}/QtDesigner
    ${QT_LIBRARY_DIR}/QtDesigner.framework/Headers
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
    )

  # Set QT_QTASSISTANT_INCLUDE_DIR
  find_path(QT_QTASSISTANT_INCLUDE_DIR QtAssistant
    PATHS
    ${QT_HEADERS_DIR}/QtAssistant
    ${QT_LIBRARY_DIR}/QtAssistant.framework/Headers
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
    )

  # Set QT_QTASSISTANTCLIENT_INCLUDE_DIR
  find_path(QT_QTASSISTANTCLIENT_INCLUDE_DIR QAssistantClient
    PATHS
    ${QT_HEADERS_DIR}/QtAssistant
    ${QT_LIBRARY_DIR}/QtAssistant.framework/Headers
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
    )

  ########################################
  #
  #       Setting the LIBRARY-Variables
  #
  ########################################

  # find the libraries
  foreach(QT_MODULE ${QT_MODULES})
    string(TOUPPER ${QT_MODULE} _upper_qt_module)
    find_library(QT_${_upper_qt_module}_LIBRARY_RELEASE
                 NAMES ${QT_MODULE}${QT_LIBINFIX} ${QT_MODULE}${QT_LIBINFIX}4
                 PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
        )
    find_library(QT_${_upper_qt_module}_LIBRARY_DEBUG
                 NAMES ${QT_MODULE}${QT_LIBINFIX}_debug ${QT_MODULE}${QT_LIBINFIX}d ${QT_MODULE}${QT_LIBINFIX}d4
                 PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
        )
    if(QT_${_upper_qt_module}_LIBRARY_RELEASE MATCHES "/${QT_MODULE}\\.framework$")
      if(NOT EXISTS "${QT_${_upper_qt_module}_LIBRARY_RELEASE}/${QT_MODULE}")
        # Release framework library file does not exist... Force to NOTFOUND:
        set(QT_${_upper_qt_module}_LIBRARY_RELEASE "QT_${_upper_qt_module}_LIBRARY_RELEASE-NOTFOUND" CACHE FILEPATH "Path to a library." FORCE)
      endif()
    endif()
    if(QT_${_upper_qt_module}_LIBRARY_DEBUG MATCHES "/${QT_MODULE}\\.framework$")
      if(NOT EXISTS "${QT_${_upper_qt_module}_LIBRARY_DEBUG}/${QT_MODULE}")
        # Debug framework library file does not exist... Force to NOTFOUND:
        set(QT_${_upper_qt_module}_LIBRARY_DEBUG "QT_${_upper_qt_module}_LIBRARY_DEBUG-NOTFOUND" CACHE FILEPATH "Path to a library." FORCE)
      endif()
    endif()
  endforeach()

  # QtUiTools is sometimes not in the same directory as the other found libraries
  # e.g. on Mac, its never a framework like the others are
  if(QT_QTCORE_LIBRARY_RELEASE AND NOT QT_QTUITOOLS_LIBRARY_RELEASE)
    find_library(QT_QTUITOOLS_LIBRARY_RELEASE NAMES QtUiTools${QT_LIBINFIX} PATHS ${QT_LIBRARY_DIR})
  endif()

  # Set QT_QTDESIGNERCOMPONENTS_LIBRARY
  find_library(QT_QTDESIGNERCOMPONENTS_LIBRARY_RELEASE NAMES QtDesignerComponents${QT_LIBINFIX} QtDesignerComponents${QT_LIBINFIX}4 PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  find_library(QT_QTDESIGNERCOMPONENTS_LIBRARY_DEBUG   NAMES QtDesignerComponents${QT_LIBINFIX}_debug QtDesignerComponents${QT_LIBINFIX}d QtDesignerComponents${QT_LIBINFIX}d4 PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

  # Set QT_QTMAIN_LIBRARY
  if(Q_WS_WIN)
    find_library(QT_QTMAIN_LIBRARY_RELEASE NAMES qtmain${QT_LIBINFIX} PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    find_library(QT_QTMAIN_LIBRARY_DEBUG NAMES qtmain${QT_LIBINFIX}d PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  endif()

  # Set QT_QTASSISTANTCLIENT_LIBRARY
  find_library(QT_QTASSISTANTCLIENT_LIBRARY_RELEASE NAMES QtAssistantClient${QT_LIBINFIX} QtAssistantClient${QT_LIBINFIX}4 PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  find_library(QT_QTASSISTANTCLIENT_LIBRARY_DEBUG   NAMES QtAssistantClient${QT_LIBINFIX}_debug QtAssistantClient${QT_LIBINFIX}d QtAssistantClient${QT_LIBINFIX}d4 PATHS ${QT_LIBRARY_DIR}  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

  # Set QT_QTASSISTANT_LIBRARY
  find_library(QT_QTASSISTANT_LIBRARY_RELEASE NAMES QtAssistantClient${QT_LIBINFIX} QtAssistantClient${QT_LIBINFIX}4 QtAssistant${QT_LIBINFIX} QtAssistant${QT_LIBINFIX}4 PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  find_library(QT_QTASSISTANT_LIBRARY_DEBUG   NAMES QtAssistantClient${QT_LIBINFIX}_debug QtAssistantClient${QT_LIBINFIX}d QtAssistantClient${QT_LIBINFIX}d4 QtAssistant${QT_LIBINFIX}_debug QtAssistant${QT_LIBINFIX}d4 PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

  # Set QT_QTHELP_LIBRARY
  find_library(QT_QTCLUCENE_LIBRARY_RELEASE NAMES QtCLucene${QT_LIBINFIX} QtCLucene${QT_LIBINFIX}4 PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  find_library(QT_QTCLUCENE_LIBRARY_DEBUG   NAMES QtCLucene${QT_LIBINFIX}_debug QtCLucene${QT_LIBINFIX}d QtCLucene${QT_LIBINFIX}d4 PATHS ${QT_LIBRARY_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  if(Q_WS_MAC AND QT_QTCORE_LIBRARY_RELEASE AND NOT QT_QTCLUCENE_LIBRARY_RELEASE)
    find_library(QT_QTCLUCENE_LIBRARY_RELEASE NAMES QtCLucene${QT_LIBINFIX} PATHS ${QT_LIBRARY_DIR})
  endif()


  ############################################
  #
  # Check the existence of the libraries.
  #
  ############################################


  macro(_qt4_add_target_depends_internal _QT_MODULE _PROPERTY)
    if (TARGET Qt4::${_QT_MODULE})
      foreach(_DEPEND ${ARGN})
        set(_VALID_DEPENDS)
        if (TARGET Qt4::Qt${_DEPEND})
          list(APPEND _VALID_DEPENDS Qt4::Qt${_DEPEND})
        endif()
        if (_VALID_DEPENDS)
          set_property(TARGET Qt4::${_QT_MODULE} APPEND PROPERTY
            ${_PROPERTY}
            "${_VALID_DEPENDS}"
          )
        endif()
        set(_VALID_DEPENDS)
      endforeach()
    endif()
  endmacro()

  macro(_qt4_add_target_depends _QT_MODULE)
    if (TARGET Qt4::${_QT_MODULE})
      get_target_property(_configs Qt4::${_QT_MODULE} IMPORTED_CONFIGURATIONS)
      _qt4_add_target_depends_internal(${_QT_MODULE} INTERFACE_LINK_LIBRARIES ${ARGN})
      foreach(_config ${_configs})
        _qt4_add_target_depends_internal(${_QT_MODULE} IMPORTED_LINK_INTERFACE_LIBRARIES_${_config} ${ARGN})
      endforeach()
      set(_configs)
    endif()
  endmacro()

  macro(_qt4_add_target_private_depends _QT_MODULE)
    if (TARGET Qt4::${_QT_MODULE})
      get_target_property(_configs Qt4::${_QT_MODULE} IMPORTED_CONFIGURATIONS)
      foreach(_config ${_configs})
        _qt4_add_target_depends_internal(${_QT_MODULE} IMPORTED_LINK_DEPENDENT_LIBRARIES_${_config} ${ARGN})
      endforeach()
      set(_configs)
    endif()
  endmacro()


  # Set QT_xyz_LIBRARY variable and add
  # library include path to QT_INCLUDES
  _QT4_ADJUST_LIB_VARS(QtCore)
  set_property(TARGET Qt4::QtCore APPEND PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES
      "${QT_MKSPECS_DIR}/default"
      ${QT_INCLUDE_DIR}
  )
  set_property(TARGET Qt4::QtCore APPEND PROPERTY
    INTERFACE_COMPILE_DEFINITIONS
      $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG>
  )
  set_property(TARGET Qt4::QtCore PROPERTY
    INTERFACE_QT_MAJOR_VERSION 4
  )
  set_property(TARGET Qt4::QtCore APPEND PROPERTY
    COMPATIBLE_INTERFACE_STRING QT_MAJOR_VERSION
  )

  foreach(QT_MODULE ${QT_MODULES})
    _QT4_ADJUST_LIB_VARS(${QT_MODULE})
    _qt4_add_target_depends(${QT_MODULE} Core)
  endforeach()

  _QT4_ADJUST_LIB_VARS(QtAssistant)
  _QT4_ADJUST_LIB_VARS(QtAssistantClient)
  _QT4_ADJUST_LIB_VARS(QtCLucene)
  _QT4_ADJUST_LIB_VARS(QtDesignerComponents)

  # platform dependent libraries
  if(Q_WS_WIN)
    _QT4_ADJUST_LIB_VARS(qtmain)

    _QT4_ADJUST_LIB_VARS(QAxServer)
    if(QT_QAXSERVER_FOUND)
      set_property(TARGET Qt4::QAxServer PROPERTY
        INTERFACE_QT4_NO_LINK_QTMAIN ON
      )
      set_property(TARGET Qt4::QAxServer APPEND PROPERTY
        COMPATIBLE_INTERFACE_BOOL QT4_NO_LINK_QTMAIN)
    endif()

    _QT4_ADJUST_LIB_VARS(QAxContainer)
  endif()

  # Only public dependencies are listed here.
  # Eg, QtDBus links to QtXml, but users of QtDBus do not need to
  # link to QtXml because QtDBus only uses it internally, not in public
  # headers.
  # Everything depends on QtCore, but that is covered above already
  _qt4_add_target_depends(Qt3Support Sql Gui Network)
  if (TARGET Qt4::Qt3Support)
    # An additional define is required for QT3_SUPPORT
    set_property(TARGET Qt4::Qt3Support APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS QT3_SUPPORT)
  endif()
  _qt4_add_target_depends(QtDeclarative Script Gui)
  _qt4_add_target_depends(QtDesigner Gui)
  _qt4_add_target_depends(QtHelp Gui)
  _qt4_add_target_depends(QtMultimedia Gui)
  _qt4_add_target_depends(QtOpenGL Gui)
  _qt4_add_target_depends(QtSvg Gui)
  _qt4_add_target_depends(QtWebKit Gui Network)

  _qt4_add_target_private_depends(Qt3Support Xml)
  if(QT_VERSION VERSION_GREATER 4.6)
    _qt4_add_target_private_depends(QtSvg Xml)
  endif()
  _qt4_add_target_private_depends(QtDBus Xml)
  _qt4_add_target_private_depends(QtUiTools Xml Gui)
  _qt4_add_target_private_depends(QtHelp Sql Xml Network)
  _qt4_add_target_private_depends(QtXmlPatterns Network)
  _qt4_add_target_private_depends(QtScriptTools Gui)
  _qt4_add_target_private_depends(QtWebKit XmlPatterns)
  _qt4_add_target_private_depends(QtDeclarative XmlPatterns Svg Sql Gui)
  _qt4_add_target_private_depends(QtMultimedia Gui)
  _qt4_add_target_private_depends(QtOpenGL Gui)
  if(QT_QAXSERVER_FOUND)
    _qt4_add_target_private_depends(QAxServer Gui)
  endif()
  if(QT_QAXCONTAINER_FOUND)
    _qt4_add_target_private_depends(QAxContainer Gui)
  endif()
  _qt4_add_target_private_depends(phonon Gui)
  if(QT_QTDBUS_FOUND)
    _qt4_add_target_private_depends(phonon DBus)
  endif()

  if (WIN32 AND NOT QT4_NO_LINK_QTMAIN)
    set(_isExe $<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>)
    set(_isWin32 $<BOOL:$<TARGET_PROPERTY:WIN32_EXECUTABLE>>)
    set(_isNotExcluded $<NOT:$<BOOL:$<TARGET_PROPERTY:QT4_NO_LINK_QTMAIN>>>)
    set(_isPolicyNEW $<TARGET_POLICY:CMP0020>)
    get_target_property(_configs Qt4::QtCore IMPORTED_CONFIGURATIONS)
    set_property(TARGET Qt4::QtCore APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES
          $<$<AND:${_isExe},${_isWin32},${_isNotExcluded},${_isPolicyNEW}>:Qt4::qtmain>
    )
    foreach(_config ${_configs})
      set_property(TARGET Qt4::QtCore APPEND PROPERTY
        IMPORTED_LINK_INTERFACE_LIBRARIES_${_config}
          $<$<AND:${_isExe},${_isWin32},${_isNotExcluded},${_isPolicyNEW}>:Qt4::qtmain>
      )
    endforeach()
    unset(_configs)
    unset(_isExe)
    unset(_isWin32)
    unset(_isNotExcluded)
    unset(_isPolicyNEW)
  endif()

  #######################################
  #
  #       Check the executables of Qt
  #          ( moc, uic, rcc )
  #
  #######################################


  if(QT_QMAKE_CHANGED)
    set(QT_UIC_EXECUTABLE NOTFOUND)
    set(QT_MOC_EXECUTABLE NOTFOUND)
    set(QT_UIC3_EXECUTABLE NOTFOUND)
    set(QT_RCC_EXECUTABLE NOTFOUND)
    set(QT_DBUSCPP2XML_EXECUTABLE NOTFOUND)
    set(QT_DBUSXML2CPP_EXECUTABLE NOTFOUND)
    set(QT_LUPDATE_EXECUTABLE NOTFOUND)
    set(QT_LRELEASE_EXECUTABLE NOTFOUND)
    set(QT_QCOLLECTIONGENERATOR_EXECUTABLE NOTFOUND)
    set(QT_DESIGNER_EXECUTABLE NOTFOUND)
    set(QT_LINGUIST_EXECUTABLE NOTFOUND)
  endif()

  macro(_find_qt4_program VAR NAME)
    find_program(${VAR}
      NAMES ${ARGN}
      PATHS ${QT_BINARY_DIR}
      NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
      )
    if (${VAR} AND NOT TARGET ${NAME})
      add_executable(${NAME} IMPORTED)
      set_property(TARGET ${NAME} PROPERTY IMPORTED_LOCATION ${${VAR}})
    endif()
  endmacro()

  _find_qt4_program(QT_MOC_EXECUTABLE Qt4::moc moc-qt4 moc4 moc)
  _find_qt4_program(QT_UIC_EXECUTABLE Qt4::uic uic-qt4 uic4 uic)
  _find_qt4_program(QT_UIC3_EXECUTABLE Qt4::uic3 uic3)
  _find_qt4_program(QT_RCC_EXECUTABLE Qt4::rcc rcc)
  _find_qt4_program(QT_DBUSCPP2XML_EXECUTABLE Qt4::qdbuscpp2xml qdbuscpp2xml)
  _find_qt4_program(QT_DBUSXML2CPP_EXECUTABLE Qt4::qdbusxml2cpp qdbusxml2cpp)
  _find_qt4_program(QT_LUPDATE_EXECUTABLE Qt4::lupdate lupdate-qt4 lupdate4 lupdate)
  _find_qt4_program(QT_LRELEASE_EXECUTABLE Qt4::lrelease lrelease-qt4 lrelease4 lrelease)
  _find_qt4_program(QT_QCOLLECTIONGENERATOR_EXECUTABLE Qt4::qcollectiongenerator qcollectiongenerator-qt4 qcollectiongenerator)
  _find_qt4_program(QT_DESIGNER_EXECUTABLE Qt4::designer designer-qt4 designer4 designer)
  _find_qt4_program(QT_LINGUIST_EXECUTABLE Qt4::linguist linguist-qt4 linguist4 linguist)

  if (NOT TARGET Qt4::qmake)
    add_executable(Qt4::qmake IMPORTED)
    set_property(TARGET Qt4::qmake PROPERTY IMPORTED_LOCATION ${QT_QMAKE_EXECUTABLE})
  endif()

  if (QT_MOC_EXECUTABLE)
     set(QT_WRAP_CPP "YES")
  endif ()

  if (QT_UIC_EXECUTABLE)
     set(QT_WRAP_UI "YES")
  endif ()



  mark_as_advanced( QT_UIC_EXECUTABLE QT_UIC3_EXECUTABLE QT_MOC_EXECUTABLE
    QT_RCC_EXECUTABLE QT_DBUSXML2CPP_EXECUTABLE QT_DBUSCPP2XML_EXECUTABLE
    QT_LUPDATE_EXECUTABLE QT_LRELEASE_EXECUTABLE QT_QCOLLECTIONGENERATOR_EXECUTABLE
    QT_DESIGNER_EXECUTABLE QT_LINGUIST_EXECUTABLE)


  # get the directory of the current file, used later on in the file
  get_filename_component( _qt4_current_dir  "${CMAKE_CURRENT_LIST_FILE}" PATH)


  ###############################################
  #
  #       configuration/system dependent settings
  #
  ###############################################

  include("${_qt4_current_dir}/Qt4ConfigDependentSettings.cmake")


  #######################################
  #
  #       Check the plugins of Qt
  #
  #######################################

  set( QT_PLUGIN_TYPES accessible bearer codecs decorations designer gfxdrivers graphicssystems iconengines imageformats inputmethods mousedrivers phonon_backend script sqldrivers )

  set( QT_ACCESSIBLE_PLUGINS qtaccessiblecompatwidgets qtaccessiblewidgets )
  set( QT_BEARER_PLUGINS qcorewlanbearer qgenericbearer qnativewifibearer )
  set( QT_CODECS_PLUGINS qcncodecs qjpcodecs qkrcodecs qtwcodecs )
  set( QT_DECORATIONS_PLUGINS qdecorationdefault qdecorationwindows )
  set( QT_DESIGNER_PLUGINS arthurplugin containerextension customwidgetplugin phononwidgets qdeclarativeview qt3supportwidgets qwebview taskmenuextension worldtimeclockplugin )
  set( QT_GRAPHICSDRIVERS_PLUGINS qgfxtransformed qgfxvnc qscreenvfb )
  set( QT_GRAPHICSSYSTEMS_PLUGINS qglgraphicssystem qtracegraphicssystem )
  set( QT_ICONENGINES_PLUGINS qsvgicon )
  set( QT_IMAGEFORMATS_PLUGINS qgif qjpeg qmng qico qsvg qtiff qtga )
  set( QT_INPUTMETHODS_PLUGINS qimsw_multi )
  set( QT_MOUSEDRIVERS_PLUGINS qwstslibmousehandler )
  if(APPLE)
    set( QT_PHONON_BACKEND_PLUGINS phonon_qt7 )
  elseif(WIN32)
    set( QT_PHONON_BACKEND_PLUGINS phonon_ds9 )
  endif()
  set( QT_SCRIPT_PLUGINS qtscriptdbus )
  set( QT_SQLDRIVERS_PLUGINS qsqldb2 qsqlibase qsqlite qsqlite2 qsqlmysql qsqloci qsqlodbc qsqlpsql qsqltds )

  set( QT_PHONON_PLUGINS ${QT_PHONON_BACKEND_PLUGINS} )
  set( QT_QT3SUPPORT_PLUGINS qtaccessiblecompatwidgets )
  set( QT_QTCORE_PLUGINS ${QT_BEARER_PLUGINS} ${QT_CODECS_PLUGINS} )
  set( QT_QTGUI_PLUGINS qtaccessiblewidgets ${QT_IMAGEFORMATS_PLUGINS} ${QT_DECORATIONS_PLUGINS} ${QT_GRAPHICSDRIVERS_PLUGINS} ${QT_GRAPHICSSYSTEMS_PLUGINS} ${QT_INPUTMETHODS_PLUGINS} ${QT_MOUSEDRIVERS_PLUGINS} )
  set( QT_QTSCRIPT_PLUGINS ${QT_SCRIPT_PLUGINS} )
  set( QT_QTSQL_PLUGINS ${QT_SQLDRIVERS_PLUGINS} )
  set( QT_QTSVG_PLUGINS qsvg qsvgicon )

  if(QT_QMAKE_CHANGED)
    foreach(QT_PLUGIN_TYPE ${QT_PLUGIN_TYPES})
      string(TOUPPER ${QT_PLUGIN_TYPE} _upper_qt_plugin_type)
      set(QT_${_upper_qt_plugin_type}_PLUGINS_DIR ${QT_PLUGINS_DIR}/${QT_PLUGIN_TYPE})
      foreach(QT_PLUGIN ${QT_${_upper_qt_plugin_type}_PLUGINS})
        string(TOUPPER ${QT_PLUGIN} _upper_qt_plugin)
        unset(QT_${_upper_qt_plugin}_LIBRARY_RELEASE CACHE)
        unset(QT_${_upper_qt_plugin}_LIBRARY_DEBUG CACHE)
        unset(QT_${_upper_qt_plugin}_LIBRARY CACHE)
        unset(QT_${_upper_qt_plugin}_PLUGIN_RELEASE CACHE)
        unset(QT_${_upper_qt_plugin}_PLUGIN_DEBUG CACHE)
        unset(QT_${_upper_qt_plugin}_PLUGIN CACHE)
      endforeach()
    endforeach()
  endif()

  # find_library works better than find_file but we need to set prefixes to only match plugins
  foreach(QT_PLUGIN_TYPE ${QT_PLUGIN_TYPES})
    string(TOUPPER ${QT_PLUGIN_TYPE} _upper_qt_plugin_type)
    set(QT_${_upper_qt_plugin_type}_PLUGINS_DIR ${QT_PLUGINS_DIR}/${QT_PLUGIN_TYPE})
    foreach(QT_PLUGIN ${QT_${_upper_qt_plugin_type}_PLUGINS})
      string(TOUPPER ${QT_PLUGIN} _upper_qt_plugin)
      if(QT_IS_STATIC)
        find_library(QT_${_upper_qt_plugin}_LIBRARY_RELEASE
                     NAMES ${QT_PLUGIN}${QT_LIBINFIX} ${QT_PLUGIN}${QT_LIBINFIX}4
                     PATHS ${QT_${_upper_qt_plugin_type}_PLUGINS_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
            )
        find_library(QT_${_upper_qt_plugin}_LIBRARY_DEBUG
                     NAMES ${QT_PLUGIN}${QT_LIBINFIX}_debug ${QT_PLUGIN}${QT_LIBINFIX}d ${QT_PLUGIN}${QT_LIBINFIX}d4
                     PATHS ${QT_${_upper_qt_plugin_type}_PLUGINS_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
            )
        _QT4_ADJUST_LIB_VARS(${QT_PLUGIN})
      else()
        # find_library works easier/better than find_file but we need to set suffixes to only match plugins
        set(CMAKE_FIND_LIBRARY_SUFFIXES_DEFAULT ${CMAKE_FIND_LIBRARY_SUFFIXES})
        set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_SHARED_MODULE_SUFFIX} ${CMAKE_SHARED_LIBRARY_SUFFIX})
        find_library(QT_${_upper_qt_plugin}_PLUGIN_RELEASE
                     NAMES ${QT_PLUGIN}${QT_LIBINFIX} ${QT_PLUGIN}${QT_LIBINFIX}4
                     PATHS ${QT_${_upper_qt_plugin_type}_PLUGINS_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
            )
        find_library(QT_${_upper_qt_plugin}_PLUGIN_DEBUG
                     NAMES ${QT_PLUGIN}${QT_LIBINFIX}_debug ${QT_PLUGIN}${QT_LIBINFIX}d ${QT_PLUGIN}${QT_LIBINFIX}d4
                     PATHS ${QT_${_upper_qt_plugin_type}_PLUGINS_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH
            )
        mark_as_advanced(QT_${_upper_qt_plugin}_PLUGIN_RELEASE QT_${_upper_qt_plugin}_PLUGIN_DEBUG)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES_DEFAULT})
      endif()
    endforeach()
  endforeach()


  ######################################
  #
  #       Macros for building Qt files
  #
  ######################################

  include("${_qt4_current_dir}/Qt4Macros.cmake")

endif()

#support old QT_MIN_VERSION if set, but not if version is supplied by find_package()
if(NOT Qt4_FIND_VERSION AND QT_MIN_VERSION)
  set(Qt4_FIND_VERSION ${QT_MIN_VERSION})
endif()

if( Qt4_FIND_COMPONENTS )

  # if components specified in find_package(), make sure each of those pieces were found
  set(_QT4_FOUND_REQUIRED_VARS QT_QMAKE_EXECUTABLE QT_MOC_EXECUTABLE QT_RCC_EXECUTABLE QT_INCLUDE_DIR QT_LIBRARY_DIR)
  foreach( component ${Qt4_FIND_COMPONENTS} )
    string( TOUPPER ${component} _COMPONENT )
    if(${_COMPONENT} STREQUAL "QTMAIN")
      if(Q_WS_WIN)
        set(_QT4_FOUND_REQUIRED_VARS ${_QT4_FOUND_REQUIRED_VARS} QT_${_COMPONENT}_LIBRARY)
      endif()
    else()
      set(_QT4_FOUND_REQUIRED_VARS ${_QT4_FOUND_REQUIRED_VARS} QT_${_COMPONENT}_INCLUDE_DIR QT_${_COMPONENT}_LIBRARY)
    endif()
  endforeach()

  if(Qt4_FIND_COMPONENTS MATCHES QtGui)
    set(_QT4_FOUND_REQUIRED_VARS ${_QT4_FOUND_REQUIRED_VARS} QT_UIC_EXECUTABLE)
  endif()

else()

  # if no components specified, we'll make a default set of required variables to say Qt is found
  set(_QT4_FOUND_REQUIRED_VARS QT_QMAKE_EXECUTABLE QT_MOC_EXECUTABLE QT_RCC_EXECUTABLE QT_UIC_EXECUTABLE QT_INCLUDE_DIR
    QT_LIBRARY_DIR QT_QTCORE_LIBRARY)

endif()

if (NOT QT_VERSION_MAJOR EQUAL 4)
    set(VERSION_MSG "Found unsuitable Qt version \"${QTVERSION}\" from ${QT_QMAKE_EXECUTABLE}")
    set(Qt4_FOUND FALSE)
    if(Qt4_FIND_REQUIRED)
       message( FATAL_ERROR "${VERSION_MSG}, this code requires Qt 4.x")
    else()
      if(NOT Qt4_FIND_QUIETLY)
         message( STATUS    "${VERSION_MSG}")
      endif()
    endif()
else()
  if (CMAKE_FIND_PACKAGE_NAME STREQUAL "Qt")
    # FindQt include()'s this module. It's an old pattern, but rather than
    # trying to suppress this from outside the module (which is then sensitive
    # to the contents, detect the case in this module and suppress it
    # explicitly.
    set(FPHSA_NAME_MISMATCHED 1)
  endif ()
  FIND_PACKAGE_HANDLE_STANDARD_ARGS(Qt4 FOUND_VAR Qt4_FOUND
    REQUIRED_VARS ${_QT4_FOUND_REQUIRED_VARS}
    VERSION_VAR QTVERSION
    )
  unset(FPHSA_NAME_MISMATCHED)
endif()

#######################################
#
#       compatibility settings
#
#######################################
# Backwards compatibility for CMake1.4 and 1.2
set (QT_MOC_EXE ${QT_MOC_EXECUTABLE} )
set (QT_UIC_EXE ${QT_UIC_EXECUTABLE} )
set( QT_QT_LIBRARY "")
set(QT4_FOUND ${Qt4_FOUND})
set(QT_FOUND ${Qt4_FOUND})

