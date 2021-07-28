# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
UseJava
-------

Use Module for Java

This file provides functions for Java.  It is assumed that
:module:`FindJava` has already been loaded.  See :module:`FindJava` for
information on how to load Java into your CMake project.

Creating And Installing JARs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  add_jar(<target_name>
          [SOURCES] <source1> [<source2>...] [<resource1>...]
          [INCLUDE_JARS <jar1> [<jar2>...]]
          [ENTRY_POINT <entry>]
          [VERSION <version>]
          [OUTPUT_NAME <name>]
          [OUTPUT_DIR <dir>]
          [GENERATE_NATIVE_HEADERS <target> [DESTINATION <dir>]]
          )

This command creates a ``<target_name>.jar``.  It compiles the given
``<source>`` files and adds the given ``<resource>`` files to
the jar file.  Source files can be java files or listing files
(prefixed by ``@``).  If only resource files are given then just a jar file
is created.  The list of ``INCLUDE_JARS`` are added to the classpath when
compiling the java sources and also to the dependencies of the target.
``INCLUDE_JARS`` also accepts other target names created by ``add_jar()``.
For backwards compatibility, jar files listed as sources are ignored (as
they have been since the first version of this module).

The default ``OUTPUT_DIR`` can also be changed by setting the variable
``CMAKE_JAVA_TARGET_OUTPUT_DIR``.

Optionally, using option ``GENERATE_NATIVE_HEADERS``, native header files can
be generated for methods declared as native.  These files provide the
connective glue that allow your Java and C code to interact.  An INTERFACE
target will be created for an easy usage of generated files.  Sub-option
``DESTINATION`` can be used to specify the output directory for generated
header files.

``GENERATE_NATIVE_HEADERS`` option requires, at least, version 1.8 of the JDK.

The ``add_jar()`` function sets the following target properties on
``<target_name>``:

``INSTALL_FILES``
  The files which should be installed.  This is used by ``install_jar()``.
``JNI_SYMLINK``
  The JNI symlink which should be installed.  This is used by
  ``install_jni_symlink()``.
``JAR_FILE``
  The location of the jar file so that you can include it.
``CLASSDIR``
  The directory where the class files can be found.  For example to use them
  with ``javah``.

.. code-block:: cmake

 install_jar(<target_name> <destination>)
 install_jar(<target_name> DESTINATION <destination> [COMPONENT <component>])

This command installs the ``<target_name>`` files to the given
``<destination>``.  It should be called in the same scope as ``add_jar()`` or
it will fail.

The ``install_jar()`` function sets the ``INSTALL_DESTINATION`` target
property on jars so installed.  This property holds the ``<destination>`` as
described above, and is used by ``install_jar_exports()``.  You can get this
information with :command:`get_property` and the ``INSTALL_DESTINATION``
property key.

.. code-block:: cmake

 install_jni_symlink(<target_name> <destination>)
 install_jni_symlink(<target_name> DESTINATION <destination> [COMPONENT <component>])

This command installs the ``<target_name>`` JNI symlinks to the given
``<destination>``.  It should be called in the same scope as ``add_jar()`` or
it will fail.

.. code-block:: cmake

 install_jar_exports(TARGETS <jars>...
                     [NAMESPACE <namespace>]
                     FILE <filename>
                     DESTINATION <destination> [COMPONENT <component>])

This command installs a target export file ``<filename>`` for the named jar
targets to the given ``<destination>`` directory.  Its function is similar to
that of :command:`install(EXPORTS)`.

.. code-block:: cmake

 export_jars(TARGETS <jars>...
             [NAMESPACE <namespace>]
             FILE <filename>)

This command writes a target export file ``<filename>`` for the named ``<jars>``
targets.  Its function is similar to that of :command:`export`.


Examples
""""""""

To add compile flags to the target you can set these flags with the following
variable:

.. code-block:: cmake

  set(CMAKE_JAVA_COMPILE_FLAGS -nowarn)


To add a path or a jar file to the class path you can do this with the
``CMAKE_JAVA_INCLUDE_PATH`` variable.

.. code-block:: cmake

  set(CMAKE_JAVA_INCLUDE_PATH /usr/share/java/shibboleet.jar)

To use a different output name for the target you can set it with:

.. code-block:: cmake

  add_jar(foobar foobar.java OUTPUT_NAME shibboleet.jar)

To use a different output directory than ``CMAKE_CURRENT_BINARY_DIR`` you can
set it with:

.. code-block:: cmake

  add_jar(foobar foobar.java OUTPUT_DIR ${PROJECT_BINARY_DIR}/bin)

To define an entry point in your jar you can set it with the ``ENTRY_POINT``
named argument:

.. code-block:: cmake

  add_jar(example ENTRY_POINT com/examples/MyProject/Main)

To define a custom manifest for the jar, you can set it with the ``MANIFEST``
named argument:

.. code-block:: cmake

  add_jar(example MANIFEST /path/to/manifest)

To add a version to the target output name you can set it using the ``VERSION``
named argument to ``add_jar()``.  The following example will create a jar file
with the name ``shibboleet-1.0.0.jar`` and will create a symlink
``shibboleet.jar`` pointing to the jar with the version information.

.. code-block:: cmake

  add_jar(shibboleet shibbotleet.java VERSION 1.2.0)

If the target is a JNI library, utilize the following commands to
create a JNI symbolic link:

.. code-block:: cmake

  set(CMAKE_JNI_TARGET TRUE)
  add_jar(shibboleet shibbotleet.java VERSION 1.2.0)
  install_jar(shibboleet ${LIB_INSTALL_DIR}/shibboleet)
  install_jni_symlink(shibboleet ${JAVA_LIB_INSTALL_DIR})

If a single target needs to produce more than one jar from its
java source code, to prevent the accumulation of duplicate class
files in subsequent jars, set/reset ``CMAKE_JAR_CLASSES_PREFIX`` prior
to calling the ``add_jar()`` function:

.. code-block:: cmake

  set(CMAKE_JAR_CLASSES_PREFIX com/redhat/foo)
  add_jar(foo foo.java)

  set(CMAKE_JAR_CLASSES_PREFIX com/redhat/bar)
  add_jar(bar bar.java)

For an optimum usage of option ``GENERATE_NATIVE_HEADERS``, it is recommended to
include module JNI before any call to ``add_jar()``. The produced target for
native headers can then be used to compile C/C++ sources with the
:command:`target_link_libraries` command.

.. code-block:: cmake

  find_package(JNI)
  add_jar(foo foo.java GENERATE_NATIVE_HEADERS foo-native)
  add_library(bar bar.cpp)
  target_link_libraries(bar PRIVATE foo-native)


Finding JARs
^^^^^^^^^^^^

.. code-block:: cmake

  find_jar(<VAR>
           <name> | NAMES <name1> [<name2>...]
           [PATHS <path1> [<path2>... ENV <var>]]
           [VERSIONS <version1> [<version2>]]
           [DOC "cache documentation string"]
          )

This command is used to find a full path to the named jar.  A cache
entry named by ``<VAR>`` is created to store the result of this command.
If the full path to a jar is found the result is stored in the
variable and the search will not repeated unless the variable is
cleared.  If nothing is found, the result will be ``<VAR>-NOTFOUND``, and
the search will be attempted again next time ``find_jar()`` is invoked with
the same variable.  The name of the full path to a file that is
searched for is specified by the names listed after ``NAMES`` argument.
Additional search locations can be specified after the ``PATHS`` argument.
If you require special a version of a jar file you can specify it with
the ``VERSIONS`` argument.  The argument after ``DOC`` will be used for the
documentation string in the cache.


Javadoc
^^^^^^^

The ``create_javadoc()`` command can be used to create java documentation
based on files or packages.  For more details please read the javadoc manpage.

There are two main signatures for ``create_javadoc()``.  The first signature
works with package names on a path with source files.

.. code-block:: cmake

 create_javadoc(<VAR>
                PACKAGES <pkg1> [<pkg2>...]
                [SOURCEPATH <sourcepath>]
                [CLASSPATH <classpath>]
                [INSTALLPATH <install path>]
                [DOCTITLE "the documentation title"]
                [WINDOWTITLE "the title of the document"]
                [AUTHOR TRUE|FALSE]
                [USE TRUE|FALSE]
                [VERSION TRUE|FALSE]
                )

For example:

.. code-block:: cmake

  create_javadoc(my_example_doc
    PACKAGES com.example.foo com.example.bar
    SOURCEPATH "${CMAKE_CURRENT_SOURCE_DIR}"
    CLASSPATH ${CMAKE_JAVA_INCLUDE_PATH}
    WINDOWTITLE "My example"
    DOCTITLE "<h1>My example</h1>"
    AUTHOR TRUE
    USE TRUE
    VERSION TRUE
  )

The second signature for ``create_javadoc()`` works on a given list of
files.

.. code-block:: cmake

  create_javadoc(<VAR>
                 FILES <file1> [<file2>...]
                 [CLASSPATH <classpath>]
                 [INSTALLPATH <install path>]
                 [DOCTITLE "the documentation title"]
                 [WINDOWTITLE "the title of the document"]
                 [AUTHOR TRUE|FALSE]
                 [USE TRUE|FALSE]
                 [VERSION TRUE|FALSE]
                )

For example:

.. code-block:: cmake

  create_javadoc(my_example_doc
    FILES ${example_SRCS}
    CLASSPATH ${CMAKE_JAVA_INCLUDE_PATH}
    WINDOWTITLE "My example"
    DOCTITLE "<h1>My example</h1>"
    AUTHOR TRUE
    USE TRUE
    VERSION TRUE
  )

Both signatures share most of the options.  These options are the same
as what you can find in the javadoc manpage.  Please look at the
manpage for ``CLASSPATH``, ``DOCTITLE``, ``WINDOWTITLE``, ``AUTHOR``, ``USE``
and ``VERSION``.

If you don't set the ``INSTALLPATH``, then by default the documentation will
be installed to :

::

   ${CMAKE_INSTALL_PREFIX}/share/javadoc/<VAR>


Header Generation
^^^^^^^^^^^^^^^^^

.. code-block:: cmake

 create_javah(TARGET <target> | GENERATED_FILES <VAR>
              CLASSES <class>...
              [CLASSPATH <classpath>...]
              [DEPENDS <depend>...]
              [OUTPUT_NAME <path>|OUTPUT_DIR <path>]
              )

Create C header files from java classes. These files provide the connective glue
that allow your Java and C code to interact.

.. deprecated:: 3.11

.. note::

  This command will no longer be supported starting with version 10 of the JDK
  due to the `suppression of javah tool <http://openjdk.java.net/jeps/313>`_.
  The ``add_jar(GENERATE_NATIVE_HEADERS)`` command should be used instead.

There are two main signatures for ``create_javah()``.  The first signature
returns generated files through variable specified by the ``GENERATED_FILES``
option.  For example:

.. code-block:: cmake

  create_javah(GENERATED_FILES files_headers
    CLASSES org.cmake.HelloWorld
    CLASSPATH hello.jar
  )

The second signature for ``create_javah()`` creates a target which encapsulates
header files generation. E.g.

.. code-block:: cmake

  create_javah(TARGET target_headers
    CLASSES org.cmake.HelloWorld
    CLASSPATH hello.jar
  )

Both signatures share same options.

``CLASSES <class>...``
  Specifies Java classes used to generate headers.

``CLASSPATH <classpath>...``
  Specifies various paths to look up classes. Here .class files, jar files or
  targets created by command add_jar can be used.

``DEPENDS <depend>...``
  Targets on which the javah target depends.

``OUTPUT_NAME <path>``
  Concatenates the resulting header files for all the classes listed by option
  ``CLASSES`` into ``<path>``.  Same behavior as option ``-o`` of javah tool.

``OUTPUT_DIR <path>``
  Sets the directory where the header files will be generated.  Same behavior
  as option ``-d`` of javah tool.  If not specified,
  :variable:`CMAKE_CURRENT_BINARY_DIR` is used as the output directory.
#]=======================================================================]

function (__java_copy_file src dest comment)
    add_custom_command(
        OUTPUT  ${dest}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ARGS    ${src}
                ${dest}
        DEPENDS ${src}
        COMMENT ${comment}
        VERBATIM
        )
endfunction ()

function(__java_lcat VAR)
    foreach(_line IN LISTS ARGN)
        string(APPEND ${VAR} "${_line}\n")
    endforeach()

    set(${VAR} "${${VAR}}" PARENT_SCOPE)
endfunction()

function(__java_export_jar VAR TARGET PATH)
    get_target_property(_jarpath ${TARGET} JAR_FILE)
    get_filename_component(_jarname ${_jarpath} NAME)
    set(_target "${_jar_NAMESPACE}${TARGET}")
    __java_lcat(${VAR}
      "# Create imported target ${_target}"
      "add_library(${_target} IMPORTED STATIC)"
      "set_target_properties(${_target} PROPERTIES"
      "  IMPORTED_LOCATION \"${PATH}/${_jarname}\""
      "  JAR_FILE \"${PATH}/${_jarname}\")"
      ""
    )
    set(${VAR} "${${VAR}}" PARENT_SCOPE)
endfunction()

# define helper scripts
set(_JAVA_EXPORT_TARGETS_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/javaTargets.cmake.in)
set(_JAVA_SYMLINK_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/UseJavaSymlinks.cmake)

if (CMAKE_HOST_WIN32 AND NOT CYGWIN AND CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
    set(_UseJava_PATH_SEP "$<SEMICOLON>")
else ()
    set(_UseJava_PATH_SEP ":")
endif()

function(add_jar _TARGET_NAME)

    cmake_parse_arguments(_add_jar
      ""
      "VERSION;OUTPUT_DIR;OUTPUT_NAME;ENTRY_POINT;MANIFEST"
      "SOURCES;INCLUDE_JARS;GENERATE_NATIVE_HEADERS"
      ${ARGN}
    )

    # In CMake < 2.8.12, add_jar used variables which were set prior to calling
    # add_jar for customizing the behavior of add_jar. In order to be backwards
    # compatible, check if any of those variables are set, and use them to
    # initialize values of the named arguments. (Giving the corresponding named
    # argument will override the value set here.)
    #
    # New features should use named arguments only.
    if(NOT DEFINED _add_jar_VERSION AND DEFINED CMAKE_JAVA_TARGET_VERSION)
        set(_add_jar_VERSION "${CMAKE_JAVA_TARGET_VERSION}")
    endif()
    if(NOT DEFINED _add_jar_OUTPUT_DIR AND DEFINED CMAKE_JAVA_TARGET_OUTPUT_DIR)
        set(_add_jar_OUTPUT_DIR "${CMAKE_JAVA_TARGET_OUTPUT_DIR}")
    endif()
    if(NOT DEFINED _add_jar_OUTPUT_NAME AND DEFINED CMAKE_JAVA_TARGET_OUTPUT_NAME)
        set(_add_jar_OUTPUT_NAME "${CMAKE_JAVA_TARGET_OUTPUT_NAME}")
        # reset
        set(CMAKE_JAVA_TARGET_OUTPUT_NAME)
    endif()
    if(NOT DEFINED _add_jar_ENTRY_POINT AND DEFINED CMAKE_JAVA_JAR_ENTRY_POINT)
        set(_add_jar_ENTRY_POINT "${CMAKE_JAVA_JAR_ENTRY_POINT}")
    endif()

    set(_JAVA_SOURCE_FILES ${_add_jar_SOURCES} ${_add_jar_UNPARSED_ARGUMENTS})

    if (NOT DEFINED _add_jar_OUTPUT_DIR)
        set(_add_jar_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
    else()
        get_filename_component(_add_jar_OUTPUT_DIR ${_add_jar_OUTPUT_DIR} ABSOLUTE)
    endif()
    # ensure output directory exists
    file (MAKE_DIRECTORY "${_add_jar_OUTPUT_DIR}")

    if (_add_jar_ENTRY_POINT)
        set(_ENTRY_POINT_OPTION e)
        set(_ENTRY_POINT_VALUE ${_add_jar_ENTRY_POINT})
    endif ()

    if (_add_jar_MANIFEST)
        set(_MANIFEST_OPTION m)
        get_filename_component (_MANIFEST_VALUE "${_add_jar_MANIFEST}" ABSOLUTE)
    endif ()

    unset (_GENERATE_NATIVE_HEADERS)
    if (_add_jar_GENERATE_NATIVE_HEADERS)
      # Raise an error if JDK version is less than 1.8 because javac -h is not supported
      # by earlier versions.
      if (Java_VERSION VERSION_LESS 1.8)
        message (FATAL_ERROR "ADD_JAR: GENERATE_NATIVE_HEADERS is not supported with this version of Java.")
      endif()
      cmake_parse_arguments (_add_jar_GENERATE_NATIVE_HEADERS "" "DESTINATION" "" ${_add_jar_GENERATE_NATIVE_HEADERS})
      if (NOT _add_jar_GENERATE_NATIVE_HEADERS_UNPARSED_ARGUMENTS)
        message (FATAL_ERROR "ADD_JAR: GENERATE_NATIVE_HEADERS: missing required argument.")
      endif()
      list (LENGTH _add_jar_GENERATE_NATIVE_HEADERS_UNPARSED_ARGUMENTS length)
      if (length GREATER 1)
        list (REMOVE_AT _add_jar_GENERATE_NATIVE_HEADERS_UNPARSED_ARGUMENTS 0)
        message (FATAL_ERROR "ADD_JAR: GENERATE_NATIVE_HEADERS: ${_add_jar_GENERATE_NATIVE_HEADERS_UNPARSED_ARGUMENTS}: unexpected argument(s).")
      endif()
      if (NOT _add_jar_GENERATE_NATIVE_HEADERS_DESTINATION)
        set (_add_jar_GENERATE_NATIVE_HEADERS_DESTINATION "${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${_TARGET_NAME}.dir/native_headers")
      endif()

      set (_GENERATE_NATIVE_HEADERS_TARGET ${_add_jar_GENERATE_NATIVE_HEADERS_UNPARSED_ARGUMENTS})
      set (_GENERATE_NATIVE_HEADERS_OUTPUT_DIR "${_add_jar_GENERATE_NATIVE_HEADERS_DESTINATION}")
      set (_GENERATE_NATIVE_HEADERS -h "${_GENERATE_NATIVE_HEADERS_OUTPUT_DIR}")
    endif()

    if (LIBRARY_OUTPUT_PATH)
        set(CMAKE_JAVA_LIBRARY_OUTPUT_PATH ${LIBRARY_OUTPUT_PATH})
    else ()
        set(CMAKE_JAVA_LIBRARY_OUTPUT_PATH ${_add_jar_OUTPUT_DIR})
    endif ()

    set(CMAKE_JAVA_INCLUDE_PATH
        ${CMAKE_JAVA_INCLUDE_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_JAVA_OBJECT_OUTPUT_PATH}
        ${CMAKE_JAVA_LIBRARY_OUTPUT_PATH}
    )

    foreach (JAVA_INCLUDE_DIR IN LISTS CMAKE_JAVA_INCLUDE_PATH)
       string(APPEND CMAKE_JAVA_INCLUDE_PATH_FINAL "${_UseJava_PATH_SEP}${JAVA_INCLUDE_DIR}")
    endforeach()

    set(CMAKE_JAVA_CLASS_OUTPUT_PATH "${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${_TARGET_NAME}.dir")

    set(_JAVA_TARGET_OUTPUT_NAME "${_TARGET_NAME}.jar")
    if (_add_jar_OUTPUT_NAME AND _add_jar_VERSION)
        set(_JAVA_TARGET_OUTPUT_NAME "${_add_jar_OUTPUT_NAME}-${_add_jar_VERSION}.jar")
        set(_JAVA_TARGET_OUTPUT_LINK "${_add_jar_OUTPUT_NAME}.jar")
    elseif (_add_jar_VERSION)
        set(_JAVA_TARGET_OUTPUT_NAME "${_TARGET_NAME}-${_add_jar_VERSION}.jar")
        set(_JAVA_TARGET_OUTPUT_LINK "${_TARGET_NAME}.jar")
    elseif (_add_jar_OUTPUT_NAME)
        set(_JAVA_TARGET_OUTPUT_NAME "${_add_jar_OUTPUT_NAME}.jar")
    endif ()

    set(_JAVA_CLASS_FILES)
    set(_JAVA_COMPILE_FILES)
    set(_JAVA_COMPILE_FILELISTS)
    set(_JAVA_DEPENDS)
    set(_JAVA_COMPILE_DEPENDS)
    set(_JAVA_RESOURCE_FILES)
    set(_JAVA_RESOURCE_FILES_RELATIVE)
    foreach(_JAVA_SOURCE_FILE IN LISTS _JAVA_SOURCE_FILES)
        get_filename_component(_JAVA_EXT ${_JAVA_SOURCE_FILE} EXT)
        get_filename_component(_JAVA_FILE ${_JAVA_SOURCE_FILE} NAME_WE)
        get_filename_component(_JAVA_PATH ${_JAVA_SOURCE_FILE} PATH)
        get_filename_component(_JAVA_FULL ${_JAVA_SOURCE_FILE} ABSOLUTE)

        if (_JAVA_SOURCE_FILE MATCHES "^@(.+)$")
            get_filename_component(_JAVA_FULL ${CMAKE_MATCH_1} ABSOLUTE)
            list(APPEND _JAVA_COMPILE_FILELISTS ${_JAVA_FULL})

        elseif (_JAVA_EXT MATCHES ".java")
            file(RELATIVE_PATH _JAVA_REL_BINARY_PATH ${CMAKE_CURRENT_BINARY_DIR} ${_JAVA_FULL})
            file(RELATIVE_PATH _JAVA_REL_SOURCE_PATH ${CMAKE_CURRENT_SOURCE_DIR} ${_JAVA_FULL})
            string(LENGTH ${_JAVA_REL_BINARY_PATH} _BIN_LEN)
            string(LENGTH ${_JAVA_REL_SOURCE_PATH} _SRC_LEN)
            if (_BIN_LEN LESS _SRC_LEN)
                set(_JAVA_REL_PATH ${_JAVA_REL_BINARY_PATH})
            else ()
                set(_JAVA_REL_PATH ${_JAVA_REL_SOURCE_PATH})
            endif ()
            get_filename_component(_JAVA_REL_PATH ${_JAVA_REL_PATH} PATH)

            list(APPEND _JAVA_COMPILE_FILES ${_JAVA_SOURCE_FILE})
            set(_JAVA_CLASS_FILE "${CMAKE_JAVA_CLASS_OUTPUT_PATH}/${_JAVA_REL_PATH}/${_JAVA_FILE}.class")
            set(_JAVA_CLASS_FILES ${_JAVA_CLASS_FILES} ${_JAVA_CLASS_FILE})

        elseif (_JAVA_EXT MATCHES ".jar"
                OR _JAVA_EXT MATCHES ".war"
                OR _JAVA_EXT MATCHES ".ear"
                OR _JAVA_EXT MATCHES ".sar")
            # Ignored for backward compatibility

        elseif (_JAVA_EXT STREQUAL "")
            list(APPEND CMAKE_JAVA_INCLUDE_PATH ${JAVA_JAR_TARGET_${_JAVA_SOURCE_FILE}} ${JAVA_JAR_TARGET_${_JAVA_SOURCE_FILE}_CLASSPATH})
            list(APPEND _JAVA_DEPENDS ${JAVA_JAR_TARGET_${_JAVA_SOURCE_FILE}})

        else ()
            __java_copy_file(${CMAKE_CURRENT_SOURCE_DIR}/${_JAVA_SOURCE_FILE}
                             ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/${_JAVA_SOURCE_FILE}
                             "Copying ${_JAVA_SOURCE_FILE} to the build directory")
            list(APPEND _JAVA_RESOURCE_FILES ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/${_JAVA_SOURCE_FILE})
            list(APPEND _JAVA_RESOURCE_FILES_RELATIVE ${_JAVA_SOURCE_FILE})
        endif ()
    endforeach()

    foreach(_JAVA_INCLUDE_JAR IN LISTS _add_jar_INCLUDE_JARS)
        if (TARGET ${_JAVA_INCLUDE_JAR})
            get_target_property(_JAVA_JAR_PATH ${_JAVA_INCLUDE_JAR} JAR_FILE)
            if (_JAVA_JAR_PATH)
                string(APPEND CMAKE_JAVA_INCLUDE_PATH_FINAL "${_UseJava_PATH_SEP}${_JAVA_JAR_PATH}")
                list(APPEND CMAKE_JAVA_INCLUDE_PATH ${_JAVA_JAR_PATH})
                list(APPEND _JAVA_DEPENDS ${_JAVA_INCLUDE_JAR})
                list(APPEND _JAVA_COMPILE_DEPENDS ${_JAVA_JAR_PATH})
            else ()
                message(SEND_ERROR "add_jar: INCLUDE_JARS target ${_JAVA_INCLUDE_JAR} is not a jar")
            endif ()
        else ()
            string(APPEND CMAKE_JAVA_INCLUDE_PATH_FINAL "${_UseJava_PATH_SEP}${_JAVA_INCLUDE_JAR}")
            list(APPEND CMAKE_JAVA_INCLUDE_PATH "${_JAVA_INCLUDE_JAR}")
            list(APPEND _JAVA_DEPENDS "${_JAVA_INCLUDE_JAR}")
            list(APPEND _JAVA_COMPILE_DEPENDS "${_JAVA_INCLUDE_JAR}")
        endif ()
    endforeach()

    if (_JAVA_COMPILE_FILES OR _JAVA_COMPILE_FILELISTS)
        set (_JAVA_SOURCES_FILELISTS)

        if (_JAVA_COMPILE_FILES)
            # Create the list of files to compile.
            set(_JAVA_SOURCES_FILE ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_sources)
            string(REPLACE ";" "\"\n\"" _JAVA_COMPILE_STRING "\"${_JAVA_COMPILE_FILES}\"")
            set(CMAKE_CONFIGURABLE_FILE_CONTENT "${_JAVA_COMPILE_STRING}")
            configure_file("${CMAKE_ROOT}/Modules/CMakeConfigurableFile.in"
              "${_JAVA_SOURCES_FILE}" @ONLY)
            unset(CMAKE_CONFIGURABLE_FILE_CONTENT)
            list (APPEND _JAVA_SOURCES_FILELISTS "@${_JAVA_SOURCES_FILE}")
        endif()
        if (_JAVA_COMPILE_FILELISTS)
            foreach (_JAVA_FILELIST IN LISTS _JAVA_COMPILE_FILELISTS)
                list (APPEND _JAVA_SOURCES_FILELISTS "@${_JAVA_FILELIST}")
            endforeach()
        endif()

        # Compile the java files and create a list of class files
        add_custom_command(
            # NOTE: this command generates an artificial dependency file
            OUTPUT ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_compiled_${_TARGET_NAME}
            COMMAND ${CMAKE_COMMAND}
                -DCMAKE_JAVA_CLASS_OUTPUT_PATH=${CMAKE_JAVA_CLASS_OUTPUT_PATH}
                -DCMAKE_JAR_CLASSES_PREFIX=${CMAKE_JAR_CLASSES_PREFIX}
                -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/UseJava/ClearClassFiles.cmake
            COMMAND ${Java_JAVAC_EXECUTABLE}
                ${CMAKE_JAVA_COMPILE_FLAGS}
                -classpath "${CMAKE_JAVA_INCLUDE_PATH_FINAL}"
                -d ${CMAKE_JAVA_CLASS_OUTPUT_PATH}
                ${_GENERATE_NATIVE_HEADERS}
                ${_JAVA_SOURCES_FILELISTS}
            COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_compiled_${_TARGET_NAME}
            DEPENDS ${_JAVA_COMPILE_FILES} ${_JAVA_COMPILE_FILELISTS} ${_JAVA_COMPILE_DEPENDS} ${_JAVA_SOURCES_FILE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Building Java objects for ${_TARGET_NAME}.jar"
            VERBATIM
        )
        add_custom_command(
            OUTPUT ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_class_filelist
            COMMAND ${CMAKE_COMMAND}
                -DCMAKE_JAVA_CLASS_OUTPUT_PATH=${CMAKE_JAVA_CLASS_OUTPUT_PATH}
                -DCMAKE_JAR_CLASSES_PREFIX=${CMAKE_JAR_CLASSES_PREFIX}
                -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/UseJavaClassFilelist.cmake
            DEPENDS ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_compiled_${_TARGET_NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            VERBATIM
        )
    else ()
        # create an empty java_class_filelist
        if (NOT EXISTS ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_class_filelist)
            file(WRITE ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_class_filelist "")
        endif()
    endif ()

    # create the jar file
    set(_JAVA_JAR_OUTPUT_PATH
      "${_add_jar_OUTPUT_DIR}/${_JAVA_TARGET_OUTPUT_NAME}")
    if (CMAKE_JNI_TARGET)
        add_custom_command(
            OUTPUT ${_JAVA_JAR_OUTPUT_PATH}
            COMMAND ${Java_JAR_EXECUTABLE}
                -cf${_ENTRY_POINT_OPTION}${_MANIFEST_OPTION} ${_JAVA_JAR_OUTPUT_PATH} ${_ENTRY_POINT_VALUE} ${_MANIFEST_VALUE}
                ${_JAVA_RESOURCE_FILES_RELATIVE} @java_class_filelist
            COMMAND ${CMAKE_COMMAND}
                -D_JAVA_TARGET_DIR=${_add_jar_OUTPUT_DIR}
                -D_JAVA_TARGET_OUTPUT_NAME=${_JAVA_TARGET_OUTPUT_NAME}
                -D_JAVA_TARGET_OUTPUT_LINK=${_JAVA_TARGET_OUTPUT_LINK}
                -P ${_JAVA_SYMLINK_SCRIPT}
            COMMAND ${CMAKE_COMMAND}
                -D_JAVA_TARGET_DIR=${_add_jar_OUTPUT_DIR}
                -D_JAVA_TARGET_OUTPUT_NAME=${_JAVA_JAR_OUTPUT_PATH}
                -D_JAVA_TARGET_OUTPUT_LINK=${_JAVA_TARGET_OUTPUT_LINK}
                -P ${_JAVA_SYMLINK_SCRIPT}
            DEPENDS ${_JAVA_RESOURCE_FILES} ${_JAVA_DEPENDS} ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_class_filelist
            WORKING_DIRECTORY ${CMAKE_JAVA_CLASS_OUTPUT_PATH}
            COMMENT "Creating Java archive ${_JAVA_TARGET_OUTPUT_NAME}"
            VERBATIM
        )
    else ()
        add_custom_command(
            OUTPUT ${_JAVA_JAR_OUTPUT_PATH}
            COMMAND ${Java_JAR_EXECUTABLE}
                -cf${_ENTRY_POINT_OPTION}${_MANIFEST_OPTION} ${_JAVA_JAR_OUTPUT_PATH} ${_ENTRY_POINT_VALUE} ${_MANIFEST_VALUE}
                ${_JAVA_RESOURCE_FILES_RELATIVE} @java_class_filelist
            COMMAND ${CMAKE_COMMAND}
                -D_JAVA_TARGET_DIR=${_add_jar_OUTPUT_DIR}
                -D_JAVA_TARGET_OUTPUT_NAME=${_JAVA_TARGET_OUTPUT_NAME}
                -D_JAVA_TARGET_OUTPUT_LINK=${_JAVA_TARGET_OUTPUT_LINK}
                -P ${_JAVA_SYMLINK_SCRIPT}
            WORKING_DIRECTORY ${CMAKE_JAVA_CLASS_OUTPUT_PATH}
            DEPENDS ${_JAVA_RESOURCE_FILES} ${_JAVA_DEPENDS} ${CMAKE_JAVA_CLASS_OUTPUT_PATH}/java_class_filelist
            COMMENT "Creating Java archive ${_JAVA_TARGET_OUTPUT_NAME}"
            VERBATIM
        )
    endif ()

    # Add the target and make sure we have the latest resource files.
    add_custom_target(${_TARGET_NAME} ALL DEPENDS ${_JAVA_JAR_OUTPUT_PATH})

    set_property(
        TARGET
            ${_TARGET_NAME}
        PROPERTY
            INSTALL_FILES
                ${_JAVA_JAR_OUTPUT_PATH}
    )

    if (_JAVA_TARGET_OUTPUT_LINK)
        set_property(
            TARGET
                ${_TARGET_NAME}
            PROPERTY
                INSTALL_FILES
                    ${_JAVA_JAR_OUTPUT_PATH}
                    ${_add_jar_OUTPUT_DIR}/${_JAVA_TARGET_OUTPUT_LINK}
        )

        if (CMAKE_JNI_TARGET)
            set_property(
                TARGET
                    ${_TARGET_NAME}
                PROPERTY
                    JNI_SYMLINK
                        ${_add_jar_OUTPUT_DIR}/${_JAVA_TARGET_OUTPUT_LINK}
            )
        endif ()
    endif ()

    set_property(
        TARGET
            ${_TARGET_NAME}
        PROPERTY
            JAR_FILE
                ${_JAVA_JAR_OUTPUT_PATH}
    )

    set_property(
        TARGET
            ${_TARGET_NAME}
        PROPERTY
            CLASSDIR
                ${CMAKE_JAVA_CLASS_OUTPUT_PATH}
    )

  if (_GENERATE_NATIVE_HEADERS)
    # create an INTERFACE library encapsulating include directory for generated headers
    add_library (${_GENERATE_NATIVE_HEADERS_TARGET} INTERFACE)
    target_include_directories (${_GENERATE_NATIVE_HEADERS_TARGET} INTERFACE
      "${_GENERATE_NATIVE_HEADERS_OUTPUT_DIR}"
      ${JNI_INCLUDE_DIRS})
    # this INTERFACE library depends on jar generation
    add_dependencies (${_GENERATE_NATIVE_HEADERS_TARGET} ${_TARGET_NAME})

    set_property (DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
      "${_GENERATE_NATIVE_HEADERS_OUTPUT_DIR}")
  endif()
endfunction()

function(INSTALL_JAR _TARGET_NAME)
    if (ARGC EQUAL 2)
      set (_DESTINATION ${ARGV1})
    else()
      cmake_parse_arguments(_install_jar
        ""
        "DESTINATION;COMPONENT"
        ""
        ${ARGN})
      if (_install_jar_DESTINATION)
        set (_DESTINATION ${_install_jar_DESTINATION})
      else()
        message(SEND_ERROR "install_jar: ${_TARGET_NAME}: DESTINATION must be specified.")
      endif()

      if (_install_jar_COMPONENT)
        set (_COMPONENT COMPONENT ${_install_jar_COMPONENT})
      endif()
    endif()

    get_property(__FILES
        TARGET
            ${_TARGET_NAME}
        PROPERTY
            INSTALL_FILES
    )
    set_property(
        TARGET
            ${_TARGET_NAME}
        PROPERTY
            INSTALL_DESTINATION
            ${_DESTINATION}
    )

    if (__FILES)
        install(
            FILES
                ${__FILES}
            DESTINATION
                ${_DESTINATION}
            ${_COMPONENT}
        )
    else ()
        message(SEND_ERROR "install_jar: The target ${_TARGET_NAME} is not known in this scope.")
    endif ()
endfunction()

function(INSTALL_JNI_SYMLINK _TARGET_NAME)
    if (ARGC EQUAL 2)
      set (_DESTINATION ${ARGV1})
    else()
      cmake_parse_arguments(_install_jni_symlink
        ""
        "DESTINATION;COMPONENT"
        ""
        ${ARGN})
      if (_install_jni_symlink_DESTINATION)
        set (_DESTINATION ${_install_jni_symlink_DESTINATION})
      else()
        message(SEND_ERROR "install_jni_symlink: ${_TARGET_NAME}: DESTINATION must be specified.")
      endif()

      if (_install_jni_symlink_COMPONENT)
        set (_COMPONENT COMPONENT ${_install_jni_symlink_COMPONENT})
      endif()
    endif()

    get_property(__SYMLINK
        TARGET
            ${_TARGET_NAME}
        PROPERTY
            JNI_SYMLINK
    )

    if (__SYMLINK)
        install(
            FILES
                ${__SYMLINK}
            DESTINATION
                ${_DESTINATION}
            ${_COMPONENT}
        )
    else ()
        message(SEND_ERROR "install_jni_symlink: The target ${_TARGET_NAME} is not known in this scope.")
    endif ()
endfunction()

function (find_jar VARIABLE)
    set(_jar_names)
    set(_jar_files)
    set(_jar_versions)
    set(_jar_paths
        /usr/share/java/
        /usr/local/share/java/
        ${Java_JAR_PATHS})
    set(_jar_doc "NOTSET")

    set(_state "name")

    foreach (arg IN LISTS ARGN)
        if (_state STREQUAL "name")
            if (arg STREQUAL "VERSIONS")
                set(_state "versions")
            elseif (arg STREQUAL "NAMES")
                set(_state "names")
            elseif (arg STREQUAL "PATHS")
                set(_state "paths")
            elseif (arg STREQUAL "DOC")
                set(_state "doc")
            else ()
                set(_jar_names ${arg})
                if (_jar_doc STREQUAL "NOTSET")
                    set(_jar_doc "Finding ${arg} jar")
                endif ()
            endif ()
        elseif (_state STREQUAL "versions")
            if (arg STREQUAL "NAMES")
                set(_state "names")
            elseif (arg STREQUAL "PATHS")
                set(_state "paths")
            elseif (arg STREQUAL "DOC")
                set(_state "doc")
            else ()
                set(_jar_versions ${_jar_versions} ${arg})
            endif ()
        elseif (_state STREQUAL "names")
            if (arg STREQUAL "VERSIONS")
                set(_state "versions")
            elseif (arg STREQUAL "PATHS")
                set(_state "paths")
            elseif (arg STREQUAL "DOC")
                set(_state "doc")
            else ()
                set(_jar_names ${_jar_names} ${arg})
                if (_jar_doc STREQUAL "NOTSET")
                    set(_jar_doc "Finding ${arg} jar")
                endif ()
            endif ()
        elseif (_state STREQUAL "paths")
            if (arg STREQUAL "VERSIONS")
                set(_state "versions")
            elseif (arg STREQUAL "NAMES")
                set(_state "names")
            elseif (arg STREQUAL "DOC")
                set(_state "doc")
            else ()
                set(_jar_paths ${_jar_paths} ${arg})
            endif ()
        elseif (_state STREQUAL "doc")
            if (arg STREQUAL "VERSIONS")
                set(_state "versions")
            elseif (arg STREQUAL "NAMES")
                set(_state "names")
            elseif (arg STREQUAL "PATHS")
                set(_state "paths")
            else ()
                set(_jar_doc ${arg})
            endif ()
        endif ()
    endforeach ()

    if (NOT _jar_names)
        message(FATAL_ERROR "find_jar: No name to search for given")
    endif ()

    foreach (jar_name IN LISTS _jar_names)
        foreach (version IN LISTS _jar_versions)
            set(_jar_files ${_jar_files} ${jar_name}-${version}.jar)
        endforeach ()
        set(_jar_files ${_jar_files} ${jar_name}.jar)
    endforeach ()

    find_file(${VARIABLE}
        NAMES   ${_jar_files}
        PATHS   ${_jar_paths}
        DOC     ${_jar_doc}
        NO_DEFAULT_PATH)
endfunction ()

function(create_javadoc _target)
    set(_javadoc_packages)
    set(_javadoc_files)
    set(_javadoc_sourcepath)
    set(_javadoc_classpath)
    set(_javadoc_installpath "${CMAKE_INSTALL_PREFIX}/share/javadoc")
    set(_javadoc_doctitle)
    set(_javadoc_windowtitle)
    set(_javadoc_author FALSE)
    set(_javadoc_version FALSE)
    set(_javadoc_use FALSE)

    set(_state "package")

    foreach (arg IN LISTS ARGN)
        if (_state STREQUAL "package")
            if (arg STREQUAL "PACKAGES")
                set(_state "packages")
            elseif (arg STREQUAL "FILES")
                set(_state "files")
            elseif (arg STREQUAL "SOURCEPATH")
                set(_state "sourcepath")
            elseif (arg STREQUAL "CLASSPATH")
                set(_state "classpath")
            elseif (arg STREQUAL "INSTALLPATH")
                set(_state "installpath")
            elseif (arg STREQUAL "DOCTITLE")
                set(_state "doctitle")
            elseif (arg STREQUAL "WINDOWTITLE")
                set(_state "windowtitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                set(_javadoc_packages ${arg})
                set(_state "packages")
            endif ()
        elseif (_state STREQUAL "packages")
            if (arg STREQUAL "FILES")
                set(_state "files")
            elseif (arg STREQUAL "SOURCEPATH")
                set(_state "sourcepath")
            elseif (arg STREQUAL "CLASSPATH")
                set(_state "classpath")
            elseif (arg STREQUAL "INSTALLPATH")
                set(_state "installpath")
            elseif (arg STREQUAL "DOCTITLE")
                set(_state "doctitle")
            elseif (arg STREQUAL "WINDOWTITLE")
                set(_state "windowtitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                list(APPEND _javadoc_packages ${arg})
            endif ()
        elseif (_state STREQUAL "files")
            if (arg STREQUAL "PACKAGES")
                set(_state "packages")
            elseif (arg STREQUAL "SOURCEPATH")
                set(_state "sourcepath")
            elseif (arg STREQUAL "CLASSPATH")
                set(_state "classpath")
            elseif (arg STREQUAL "INSTALLPATH")
                set(_state "installpath")
            elseif (arg STREQUAL "DOCTITLE")
                set(_state "doctitle")
            elseif (arg STREQUAL "WINDOWTITLE")
                set(_state "windowtitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                list(APPEND _javadoc_files ${arg})
            endif ()
        elseif (_state STREQUAL "sourcepath")
            if (arg STREQUAL "PACKAGES")
                set(_state "packages")
            elseif (arg STREQUAL "FILES")
                set(_state "files")
            elseif (arg STREQUAL "CLASSPATH")
                set(_state "classpath")
            elseif (arg STREQUAL "INSTALLPATH")
                set(_state "installpath")
            elseif (arg STREQUAL "DOCTITLE")
                set(_state "doctitle")
            elseif (arg STREQUAL "WINDOWTITLE")
                set(_state "windowtitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                list(APPEND _javadoc_sourcepath ${arg})
            endif ()
        elseif (_state STREQUAL "classpath")
            if (arg STREQUAL "PACKAGES")
                set(_state "packages")
            elseif (arg STREQUAL "FILES")
                set(_state "files")
            elseif (arg STREQUAL "SOURCEPATH")
                set(_state "sourcepath")
            elseif (arg STREQUAL "INSTALLPATH")
                set(_state "installpath")
            elseif (arg STREQUAL "DOCTITLE")
                set(_state "doctitle")
            elseif (arg STREQUAL "WINDOWTITLE")
                set(_state "windowtitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                list(APPEND _javadoc_classpath ${arg})
            endif ()
        elseif (_state STREQUAL "installpath")
            if (arg STREQUAL "PACKAGES")
                set(_state "packages")
            elseif (arg STREQUAL "FILES")
                set(_state "files")
            elseif (arg STREQUAL "SOURCEPATH")
                set(_state "sourcepath")
            elseif (arg STREQUAL "DOCTITLE")
                set(_state "doctitle")
            elseif (arg STREQUAL "WINDOWTITLE")
                set(_state "windowtitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                set(_javadoc_installpath ${arg})
            endif ()
        elseif (_state STREQUAL "doctitle")
            if (${arg} STREQUAL "PACKAGES")
                set(_state "packages")
            elseif (arg STREQUAL "FILES")
                set(_state "files")
            elseif (arg STREQUAL "SOURCEPATH")
                set(_state "sourcepath")
            elseif (arg STREQUAL "INSTALLPATH")
                set(_state "installpath")
            elseif (arg STREQUAL "CLASSPATH")
                set(_state "classpath")
            elseif (arg STREQUAL "WINDOWTITLE")
                set(_state "windowtitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                set(_javadoc_doctitle ${arg})
            endif ()
        elseif (_state STREQUAL "windowtitle")
            if (${arg} STREQUAL "PACKAGES")
                set(_state "packages")
            elseif (arg STREQUAL "FILES")
                set(_state "files")
            elseif (arg STREQUAL "SOURCEPATH")
                set(_state "sourcepath")
            elseif (arg STREQUAL "CLASSPATH")
                set(_state "classpath")
            elseif (arg STREQUAL "INSTALLPATH")
                set(_state "installpath")
            elseif (arg STREQUAL "DOCTITLE")
                set(_state "doctitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                set(_javadoc_windowtitle ${arg})
            endif ()
        elseif (_state STREQUAL "author")
            if (arg STREQUAL "PACKAGES")
                set(_state "packages")
            elseif (arg STREQUAL "FILES")
                set(_state "files")
            elseif (arg STREQUAL "SOURCEPATH")
                set(_state "sourcepath")
            elseif (arg STREQUAL "CLASSPATH")
                set(_state "classpath")
            elseif (arg STREQUAL "INSTALLPATH")
                set(_state "installpath")
            elseif (arg STREQUAL "DOCTITLE")
                set(_state "doctitle")
            elseif (arg STREQUAL "WINDOWTITLE")
                set(_state "windowtitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                set(_javadoc_author ${arg})
            endif ()
        elseif (_state STREQUAL "use")
            if (arg STREQUAL "PACKAGES")
                set(_state "packages")
            elseif (arg STREQUAL "FILES")
                set(_state "files")
            elseif (arg STREQUAL "SOURCEPATH")
                set(_state "sourcepath")
            elseif (arg STREQUAL "CLASSPATH")
                set(_state "classpath")
            elseif (arg STREQUAL "INSTALLPATH")
                set(_state "installpath")
            elseif (arg STREQUAL "DOCTITLE")
                set(_state "doctitle")
            elseif (arg STREQUAL "WINDOWTITLE")
                set(_state "windowtitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                set(_javadoc_use ${arg})
            endif ()
        elseif (_state STREQUAL "version")
            if (arg STREQUAL "PACKAGES")
                set(_state "packages")
            elseif (arg STREQUAL "FILES")
                set(_state "files")
            elseif (arg STREQUAL "SOURCEPATH")
                set(_state "sourcepath")
            elseif (arg STREQUAL "CLASSPATH")
                set(_state "classpath")
            elseif (arg STREQUAL "INSTALLPATH")
                set(_state "installpath")
            elseif (arg STREQUAL "DOCTITLE")
                set(_state "doctitle")
            elseif (arg STREQUAL "WINDOWTITLE")
                set(_state "windowtitle")
            elseif (arg STREQUAL "AUTHOR")
                set(_state "author")
            elseif (arg STREQUAL "USE")
                set(_state "use")
            elseif (arg STREQUAL "VERSION")
                set(_state "version")
            else ()
                set(_javadoc_version ${arg})
            endif ()
        endif ()
    endforeach ()

    set(_javadoc_builddir ${CMAKE_CURRENT_BINARY_DIR}/javadoc/${_target})
    set(_javadoc_options -d ${_javadoc_builddir})

    if (_javadoc_sourcepath)
        list(JOIN _javadoc_sourcepath "${_UseJava_PATH_SEP}" _javadoc_sourcepath)
        list(APPEND _javadoc_options -sourcepath "\"${_javadoc_sourcepath}\"")
    endif ()

    if (_javadoc_classpath)
        list(JOIN _javadoc_classpath "${_UseJava_PATH_SEP}" _javadoc_classpath)
        list(APPEND _javadoc_options -classpath "\"${_javadoc_classpath}\"")
    endif ()

    if (_javadoc_doctitle)
        list(APPEND _javadoc_options -doctitle '${_javadoc_doctitle}')
    endif ()

    if (_javadoc_windowtitle)
        list(APPEND _javadoc_options -windowtitle '${_javadoc_windowtitle}')
    endif ()

    if (_javadoc_author)
        list(APPEND _javadoc_options -author)
    endif ()

    if (_javadoc_use)
        list(APPEND _javadoc_options -use)
    endif ()

    if (_javadoc_version)
        list(APPEND _javadoc_options -version)
    endif ()

    add_custom_target(${_target}_javadoc ALL
        COMMAND ${Java_JAVADOC_EXECUTABLE}
                ${_javadoc_options}
                ${_javadoc_files}
                ${_javadoc_packages}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    install(
        DIRECTORY ${_javadoc_builddir}
        DESTINATION ${_javadoc_installpath}
    )
endfunction()

function (create_javah)
  if (Java_VERSION VERSION_GREATER_EQUAL 10)
    message (FATAL_ERROR "create_javah: not supported with this Java version. Use add_jar(GENERATE_NATIVE_HEADERS) instead.")
  elseif (Java_VERSION VERSION_GREATER_EQUAL 1.8)
    message (DEPRECATION "create_javah: this command will no longer be supported starting with version 10 of JDK. Update your project by using command add_jar(GENERATE_NATIVE_HEADERS) instead.")
  endif()

    cmake_parse_arguments(_create_javah
      ""
      "TARGET;GENERATED_FILES;OUTPUT_NAME;OUTPUT_DIR"
      "CLASSES;CLASSPATH;DEPENDS"
      ${ARGN})

    # ckeck parameters
    if (NOT _create_javah_TARGET AND NOT _create_javah_GENERATED_FILES)
      message (FATAL_ERROR "create_javah: TARGET or GENERATED_FILES must be specified.")
    endif()
    if (_create_javah_OUTPUT_NAME AND _create_javah_OUTPUT_DIR)
      message (FATAL_ERROR "create_javah: OUTPUT_NAME and OUTPUT_DIR are mutually exclusive.")
    endif()

    if (NOT _create_javah_CLASSES)
      message (FATAL_ERROR "create_javah: CLASSES is a required parameter.")
    endif()

    set (_output_files)

    # handle javah options
    set (_javah_options)

    if (_create_javah_CLASSPATH)
      # CLASSPATH can specify directories, jar files or targets created with add_jar command
      set (_classpath)
      foreach (_path IN LISTS _create_javah_CLASSPATH)
        if (TARGET ${_path})
          get_target_property (_jar_path ${_path} JAR_FILE)
          if (_jar_path)
            list (APPEND _classpath "${_jar_path}")
            list (APPEND _create_javah_DEPENDS "${_path}")
          else()
            message(SEND_ERROR "create_javah: CLASSPATH target ${_path} is not a jar.")
          endif()
        elseif (EXISTS "${_path}")
          list (APPEND _classpath "${_path}")
          if (NOT IS_DIRECTORY "${_path}")
            list (APPEND _create_javah_DEPENDS "${_path}")
          endif()
        else()
          message(SEND_ERROR "create_javah: CLASSPATH entry ${_path} does not exist.")
        endif()
      endforeach()
      string (REPLACE ";" "${_UseJava_PATH_SEP}" _classpath "${_classpath}")
      list (APPEND _javah_options -classpath "${_classpath}")
    endif()

    if (_create_javah_OUTPUT_DIR)
      list (APPEND _javah_options -d "${_create_javah_OUTPUT_DIR}")
    endif()

    if (_create_javah_OUTPUT_NAME)
      list (APPEND _javah_options -o "${_create_javah_OUTPUT_NAME}")
      set (_output_files "${_create_javah_OUTPUT_NAME}")

      get_filename_component (_create_javah_OUTPUT_DIR "${_create_javah_OUTPUT_NAME}" DIRECTORY)
      get_filename_component (_create_javah_OUTPUT_DIR "${_create_javah_OUTPUT_DIR}" ABSOLUTE)
    endif()

    if (NOT _create_javah_OUTPUT_DIR)
      set (_create_javah_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    if (NOT _create_javah_OUTPUT_NAME)
      # compute output names
      foreach (_class IN LISTS _create_javah_CLASSES)
        string (REPLACE "." "_" _c_header "${_class}")
        set (_c_header  "${_create_javah_OUTPUT_DIR}/${_c_header}.h")
        list (APPEND _output_files "${_c_header}")
      endforeach()
    endif()

    # finalize custom command arguments
    if (_create_javah_DEPENDS)
      list (INSERT _create_javah_DEPENDS 0 DEPENDS)
    endif()

    add_custom_command (OUTPUT ${_output_files}
      COMMAND "${Java_JAVAH_EXECUTABLE}" ${_javah_options} -jni ${_create_javah_CLASSES}
      ${_create_javah_DEPENDS}
      WORKING_DIRECTORY ${_create_javah_OUTPUT_DIR}
      COMMENT "Building C header files from classes...")

    if (_create_javah_TARGET)
      add_custom_target (${_create_javah_TARGET} ALL DEPENDS ${_output_files})
    endif()
    if (_create_javah_GENERATED_FILES)
      set (${_create_javah_GENERATED_FILES} ${_output_files} PARENT_SCOPE)
    endif()
endfunction()

function(export_jars)
    # Parse and validate arguments
    cmake_parse_arguments(_export_jars
      ""
      "FILE;NAMESPACE"
      "TARGETS"
      ${ARGN}
    )
    if (NOT _export_jars_FILE)
      message(SEND_ERROR "export_jars: FILE must be specified.")
    endif()
    if (NOT _export_jars_TARGETS)
      message(SEND_ERROR "export_jars: TARGETS must be specified.")
    endif()
    set(_jar_NAMESPACE "${_export_jars_NAMESPACE}")

    # Set content of generated exports file
    string(REPLACE ";" " " __targets__ "${_export_jars_TARGETS}")
    set(__targetdefs__ "")
    foreach(_target IN LISTS _export_jars_TARGETS)
        get_target_property(_jarpath ${_target} JAR_FILE)
        get_filename_component(_jarpath ${_jarpath} PATH)
        __java_export_jar(__targetdefs__ ${_target} "${_jarpath}")
    endforeach()

    # Generate exports file
    configure_file(
      ${_JAVA_EXPORT_TARGETS_SCRIPT}
      ${_export_jars_FILE}
      @ONLY
    )
endfunction()

function(install_jar_exports)
    # Parse and validate arguments
    cmake_parse_arguments(_install_jar_exports
      ""
      "FILE;DESTINATION;COMPONENT;NAMESPACE"
      "TARGETS"
      ${ARGN}
    )
    if (NOT _install_jar_exports_FILE)
      message(SEND_ERROR "install_jar_exports: FILE must be specified.")
    endif()
    if (NOT _install_jar_exports_DESTINATION)
      message(SEND_ERROR "install_jar_exports: DESTINATION must be specified.")
    endif()
    if (NOT _install_jar_exports_TARGETS)
      message(SEND_ERROR "install_jar_exports: TARGETS must be specified.")
    endif()
    set(_jar_NAMESPACE "${_install_jar_exports_NAMESPACE}")

    if (_install_jar_exports_COMPONENT)
      set (_COMPONENT COMPONENT ${_install_jar_exports_COMPONENT})
    endif()

    # Determine relative path from installed export file to install prefix
    if(IS_ABSOLUTE "${_install_jar_exports_DESTINATION}")
      file(RELATIVE_PATH _relpath
        ${_install_jar_exports_DESTINATION}
        ${CMAKE_INSTALL_PREFIX}
      )
    else()
      file(RELATIVE_PATH _relpath
        ${CMAKE_INSTALL_PREFIX}/${_install_jar_exports_DESTINATION}
        ${CMAKE_INSTALL_PREFIX}
      )
    endif()

    # Set up unique location for generated exports file
    string(SHA256 _hash "${_install_jar_exports_DESTINATION}")
    set(_tmpdir ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/JavaExports/${_hash})

    # Set content of generated exports file
    string(REPLACE ";" " " __targets__ "${_install_jar_exports_TARGETS}")
    set(__targetdefs__ "set(_prefix \${CMAKE_CURRENT_LIST_DIR}/${_relpath})\n\n")
    foreach(_target IN LISTS _install_jar_exports_TARGETS)
        get_target_property(_dir ${_target} INSTALL_DESTINATION)
        __java_export_jar(__targetdefs__ ${_target} "\${_prefix}/${_dir}")
    endforeach()
    __java_lcat(__targetdefs__ "\nunset(_prefix)")

    # Generate and install exports file
    configure_file(
      ${_JAVA_EXPORT_TARGETS_SCRIPT}
      ${_tmpdir}/${_install_jar_exports_FILE}
      @ONLY
    )
    install(FILES ${_tmpdir}/${_install_jar_exports_FILE}
            DESTINATION ${_install_jar_exports_DESTINATION}
            ${_COMPONENT})
endfunction()
