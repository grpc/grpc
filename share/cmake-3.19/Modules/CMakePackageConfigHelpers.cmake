# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#[=======================================================================[.rst:
CMakePackageConfigHelpers
-------------------------

Helpers functions for creating config files that can be included by other
projects to find and use a package.

Adds the :command:`configure_package_config_file()` and
:command:`write_basic_package_version_file()` commands.

Generating a Package Configuration File
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. command:: configure_package_config_file

 Create a config file for a project::

   configure_package_config_file(<input> <output>
     INSTALL_DESTINATION <path>
     [PATH_VARS <var1> <var2> ... <varN>]
     [NO_SET_AND_CHECK_MACRO]
     [NO_CHECK_REQUIRED_COMPONENTS_MACRO]
     [INSTALL_PREFIX <path>]
     )

``configure_package_config_file()`` should be used instead of the plain
:command:`configure_file()` command when creating the ``<PackageName>Config.cmake``
or ``<PackageName>-config.cmake`` file for installing a project or library.
It helps making the resulting package relocatable by avoiding hardcoded paths
in the installed ``Config.cmake`` file.

In a ``FooConfig.cmake`` file there may be code like this to make the install
destinations know to the using project:

.. code-block:: cmake

   set(FOO_INCLUDE_DIR   "@CMAKE_INSTALL_FULL_INCLUDEDIR@" )
   set(FOO_DATA_DIR   "@CMAKE_INSTALL_PREFIX@/@RELATIVE_DATA_INSTALL_DIR@" )
   set(FOO_ICONS_DIR   "@CMAKE_INSTALL_PREFIX@/share/icons" )
   #...logic to determine installedPrefix from the own location...
   set(FOO_CONFIG_DIR  "${installedPrefix}/@CONFIG_INSTALL_DIR@" )

All 4 options shown above are not sufficient, since the first 3 hardcode the
absolute directory locations, and the 4th case works only if the logic to
determine the ``installedPrefix`` is correct, and if ``CONFIG_INSTALL_DIR``
contains a relative path, which in general cannot be guaranteed.  This has the
effect that the resulting ``FooConfig.cmake`` file would work poorly under
Windows and OSX, where users are used to choose the install location of a
binary package at install time, independent from how
:variable:`CMAKE_INSTALL_PREFIX` was set at build/cmake time.

Using ``configure_package_config_file`` helps.  If used correctly, it makes
the resulting ``FooConfig.cmake`` file relocatable.  Usage:

1. write a ``FooConfig.cmake.in`` file as you are used to
2. insert a line containing only the string ``@PACKAGE_INIT@``
3. instead of ``set(FOO_DIR "@SOME_INSTALL_DIR@")``, use
   ``set(FOO_DIR "@PACKAGE_SOME_INSTALL_DIR@")`` (this must be after the
   ``@PACKAGE_INIT@`` line)
4. instead of using the normal :command:`configure_file()`, use
   ``configure_package_config_file()``



The ``<input>`` and ``<output>`` arguments are the input and output file, the
same way as in :command:`configure_file()`.

The ``<path>`` given to ``INSTALL_DESTINATION`` must be the destination where
the ``FooConfig.cmake`` file will be installed to.  This path can either be
absolute, or relative to the ``INSTALL_PREFIX`` path.

The variables ``<var1>`` to ``<varN>`` given as ``PATH_VARS`` are the
variables which contain install destinations.  For each of them the macro will
create a helper variable ``PACKAGE_<var...>``.  These helper variables must be
used in the ``FooConfig.cmake.in`` file for setting the installed location.
They are calculated by ``configure_package_config_file`` so that they are
always relative to the installed location of the package.  This works both for
relative and also for absolute locations.  For absolute locations it works
only if the absolute location is a subdirectory of ``INSTALL_PREFIX``.

If the ``INSTALL_PREFIX`` argument is passed, this is used as base path to
calculate all the relative paths.  The ``<path>`` argument must be an absolute
path.  If this argument is not passed, the :variable:`CMAKE_INSTALL_PREFIX`
variable will be used instead.  The default value is good when generating a
FooConfig.cmake file to use your package from the install tree.  When
generating a FooConfig.cmake file to use your package from the build tree this
option should be used.

By default ``configure_package_config_file`` also generates two helper macros,
``set_and_check()`` and ``check_required_components()`` into the
``FooConfig.cmake`` file.

``set_and_check()`` should be used instead of the normal ``set()`` command for
setting directories and file locations.  Additionally to setting the variable
it also checks that the referenced file or directory actually exists and fails
with a ``FATAL_ERROR`` otherwise.  This makes sure that the created
``FooConfig.cmake`` file does not contain wrong references.
When using the ``NO_SET_AND_CHECK_MACRO``, this macro is not generated
into the ``FooConfig.cmake`` file.

``check_required_components(<PackageName>)`` should be called at the end of
the ``FooConfig.cmake`` file. This macro checks whether all requested,
non-optional components have been found, and if this is not the case, sets
the ``Foo_FOUND`` variable to ``FALSE``, so that the package is considered to
be not found.  It does that by testing the ``Foo_<Component>_FOUND``
variables for all requested required components.  This macro should be
called even if the package doesn't provide any components to make sure
users are not specifying components erroneously.  When using the
``NO_CHECK_REQUIRED_COMPONENTS_MACRO`` option, this macro is not generated
into the ``FooConfig.cmake`` file.

For an example see below the documentation for
:command:`write_basic_package_version_file()`.

Generating a Package Version File
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. command:: write_basic_package_version_file

 Create a version file for a project::

   write_basic_package_version_file(<filename>
     [VERSION <major.minor.patch>]
     COMPATIBILITY <AnyNewerVersion|SameMajorVersion|SameMinorVersion|ExactVersion>
     [ARCH_INDEPENDENT] )


Writes a file for use as ``<PackageName>ConfigVersion.cmake`` file to
``<filename>``.  See the documentation of :command:`find_package()` for
details on this.

``<filename>`` is the output filename, it should be in the build tree.
``<major.minor.patch>`` is the version number of the project to be installed.

If no ``VERSION`` is given, the :variable:`PROJECT_VERSION` variable is used.
If this hasn't been set, it errors out.

The ``COMPATIBILITY`` mode ``AnyNewerVersion`` means that the installed
package version will be considered compatible if it is newer or exactly the
same as the requested version.  This mode should be used for packages which
are fully backward compatible, also across major versions.
If ``SameMajorVersion`` is used instead, then the behaviour differs from
``AnyNewerVersion`` in that the major version number must be the same as
requested, e.g.  version 2.0 will not be considered compatible if 1.0 is
requested.  This mode should be used for packages which guarantee backward
compatibility within the same major version.
If ``SameMinorVersion`` is used, the behaviour is the same as
``SameMajorVersion``, but both major and minor version must be the same as
requested, e.g version 0.2 will not be compatible if 0.1 is requested.
If ``ExactVersion`` is used, then the package is only considered compatible if
the requested version matches exactly its own version number (not considering
the tweak version).  For example, version 1.2.3 of a package is only
considered compatible to requested version 1.2.3.  This mode is for packages
without compatibility guarantees.
If your project has more elaborated version matching rules, you will need to
write your own custom ``ConfigVersion.cmake`` file instead of using this
macro.

.. note:: ``COMPATIBILITY_MODE`` ``AnyNewerVersion``, ``SameMajorVersion`` and
  ``SameMinorVersion`` handle the version range if any is specified (see
  :command:`find_package` command for the details). ``ExactVersion`` is
  incompatible with version ranges and will display an author warning if one is
  specified.

If ``ARCH_INDEPENDENT`` is given, the installed package version will be
considered compatible even if it was built for a different architecture than
the requested architecture.  Otherwise, an architecture check will be performed,
and the package will be considered compatible only if the architecture matches
exactly.  For example, if the package is built for a 32-bit architecture, the
package is only considered compatible if it is used on a 32-bit architecture,
unless ``ARCH_INDEPENDENT`` is given, in which case the package is considered
compatible on any architecture.

.. note:: ``ARCH_INDEPENDENT`` is intended for header-only libraries or similar
   packages with no binaries.

Internally, this macro executes :command:`configure_file()` to create the
resulting version file.  Depending on the ``COMPATIBILITY``, the corresponding
``BasicConfigVersion-<COMPATIBILITY>.cmake.in`` file is used.
Please note that these files are internal to CMake and you should not call
:command:`configure_file()` on them yourself, but they can be used as starting
point to create more sophisticted custom ``ConfigVersion.cmake`` files.

Example Generating Package Files
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Example using both :command:`configure_package_config_file` and
``write_basic_package_version_file()``:

``CMakeLists.txt``:

.. code-block:: cmake

   set(INCLUDE_INSTALL_DIR include/ ... CACHE )
   set(LIB_INSTALL_DIR lib/ ... CACHE )
   set(SYSCONFIG_INSTALL_DIR etc/foo/ ... CACHE )
   #...
   include(CMakePackageConfigHelpers)
   configure_package_config_file(FooConfig.cmake.in
     ${CMAKE_CURRENT_BINARY_DIR}/FooConfig.cmake
     INSTALL_DESTINATION ${LIB_INSTALL_DIR}/Foo/cmake
     PATH_VARS INCLUDE_INSTALL_DIR SYSCONFIG_INSTALL_DIR)
   write_basic_package_version_file(
     ${CMAKE_CURRENT_BINARY_DIR}/FooConfigVersion.cmake
     VERSION 1.2.3
     COMPATIBILITY SameMajorVersion )
   install(FILES ${CMAKE_CURRENT_BINARY_DIR}/FooConfig.cmake
                 ${CMAKE_CURRENT_BINARY_DIR}/FooConfigVersion.cmake
           DESTINATION ${LIB_INSTALL_DIR}/Foo/cmake )

``FooConfig.cmake.in``:

::

   set(FOO_VERSION x.y.z)
   ...
   @PACKAGE_INIT@
   ...
   set_and_check(FOO_INCLUDE_DIR "@PACKAGE_INCLUDE_INSTALL_DIR@")
   set_and_check(FOO_SYSCONFIG_DIR "@PACKAGE_SYSCONFIG_INSTALL_DIR@")

   check_required_components(Foo)
#]=======================================================================]

include(WriteBasicConfigVersionFile)

macro(WRITE_BASIC_PACKAGE_VERSION_FILE)
  write_basic_config_version_file(${ARGN})
endmacro()

function(CONFIGURE_PACKAGE_CONFIG_FILE _inputFile _outputFile)
  set(options NO_SET_AND_CHECK_MACRO NO_CHECK_REQUIRED_COMPONENTS_MACRO)
  set(oneValueArgs INSTALL_DESTINATION INSTALL_PREFIX)
  set(multiValueArgs PATH_VARS )

  cmake_parse_arguments(CCF "${options}" "${oneValueArgs}" "${multiValueArgs}"  ${ARGN})

  if(CCF_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Unknown keywords given to CONFIGURE_PACKAGE_CONFIG_FILE(): \"${CCF_UNPARSED_ARGUMENTS}\"")
  endif()

  if(NOT CCF_INSTALL_DESTINATION)
    message(FATAL_ERROR "No INSTALL_DESTINATION given to CONFIGURE_PACKAGE_CONFIG_FILE()")
  endif()

  if(DEFINED CCF_INSTALL_PREFIX)
    if(IS_ABSOLUTE "${CCF_INSTALL_PREFIX}")
      set(installPrefix "${CCF_INSTALL_PREFIX}")
    else()
      message(FATAL_ERROR "INSTALL_PREFIX must be an absolute path")
    endif()
  elseif(IS_ABSOLUTE "${CMAKE_INSTALL_PREFIX}")
    set(installPrefix "${CMAKE_INSTALL_PREFIX}")
  else()
    get_filename_component(installPrefix "${CMAKE_INSTALL_PREFIX}" ABSOLUTE)
  endif()

  if(IS_ABSOLUTE "${CCF_INSTALL_DESTINATION}")
    set(absInstallDir "${CCF_INSTALL_DESTINATION}")
  else()
    set(absInstallDir "${installPrefix}/${CCF_INSTALL_DESTINATION}")
  endif()

  file(RELATIVE_PATH PACKAGE_RELATIVE_PATH "${absInstallDir}" "${installPrefix}" )

  foreach(var ${CCF_PATH_VARS})
    if(NOT DEFINED ${var})
      message(FATAL_ERROR "Variable ${var} does not exist")
    else()
      if(IS_ABSOLUTE "${${var}}")
        string(REPLACE "${installPrefix}" "\${PACKAGE_PREFIX_DIR}"
                        PACKAGE_${var} "${${var}}")
      else()
        set(PACKAGE_${var} "\${PACKAGE_PREFIX_DIR}/${${var}}")
      endif()
    endif()
  endforeach()

  get_filename_component(inputFileName "${_inputFile}" NAME)

  set(PACKAGE_INIT "
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was ${inputFileName}                            ########

get_filename_component(PACKAGE_PREFIX_DIR \"\${CMAKE_CURRENT_LIST_DIR}/${PACKAGE_RELATIVE_PATH}\" ABSOLUTE)
")

  if("${absInstallDir}" MATCHES "^(/usr)?/lib(64)?/.+")
    # Handle "/usr move" symlinks created by some Linux distros.
    string(APPEND PACKAGE_INIT "
# Use original install prefix when loaded through a \"/usr move\"
# cross-prefix symbolic link such as /lib -> /usr/lib.
get_filename_component(_realCurr \"\${CMAKE_CURRENT_LIST_DIR}\" REALPATH)
get_filename_component(_realOrig \"${absInstallDir}\" REALPATH)
if(_realCurr STREQUAL _realOrig)
  set(PACKAGE_PREFIX_DIR \"${installPrefix}\")
endif()
unset(_realOrig)
unset(_realCurr)
")
  endif()

  if(NOT CCF_NO_SET_AND_CHECK_MACRO)
    string(APPEND PACKAGE_INIT "
macro(set_and_check _var _file)
  set(\${_var} \"\${_file}\")
  if(NOT EXISTS \"\${_file}\")
    message(FATAL_ERROR \"File or directory \${_file} referenced by variable \${_var} does not exist !\")
  endif()
endmacro()
")
  endif()


  if(NOT CCF_NO_CHECK_REQUIRED_COMPONENTS_MACRO)
    string(APPEND PACKAGE_INIT "
macro(check_required_components _NAME)
  foreach(comp \${\${_NAME}_FIND_COMPONENTS})
    if(NOT \${_NAME}_\${comp}_FOUND)
      if(\${_NAME}_FIND_REQUIRED_\${comp})
        set(\${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()
")
  endif()

  string(APPEND PACKAGE_INIT "
####################################################################################")

  configure_file("${_inputFile}" "${_outputFile}" @ONLY)

endfunction()
