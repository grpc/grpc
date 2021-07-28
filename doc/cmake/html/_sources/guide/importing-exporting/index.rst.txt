Importing and Exporting Guide
*****************************

.. only:: html

   .. contents::

Introduction
============

In this guide, we will present the concept of :prop_tgt:`IMPORTED` targets
and demonstrate how to import existing executable or library files from disk
into a CMake project. We will then show how CMake supports exporting targets
from one CMake-based project and importing them into another. Finally, we
will demonstrate how to package a project with a configuration file to allow
for easy integration into other CMake projects. This guide and the complete
example source code can be found in the ``Help/guide/importing-exporting``
directory of the CMake source code tree.


Importing Targets
=================

:prop_tgt:`IMPORTED` targets are used to convert files outside of a CMake
project into logical targets inside of the project. :prop_tgt:`IMPORTED`
targets are created using the ``IMPORTED`` option of the
:command:`add_executable` and :command:`add_library` commands. No build
files are generated for :prop_tgt:`IMPORTED` targets. Once imported,
:prop_tgt:`IMPORTED` targets may be referenced like any other target within
the project and provide a convenient, flexible reference to outside
executables and libraries.

By default, the :prop_tgt:`IMPORTED` target name has scope in the directory in
which it is created and below. We can use the ``GLOBAL`` option to extended
visibility so that the target is accessible globally in the build system.

Details about the :prop_tgt:`IMPORTED` target are specified by setting
properties whose names begin in ``IMPORTED_`` and ``INTERFACE_``. For example,
:prop_tgt:`IMPORTED_LOCATION` contains the full path to the target on
disk.

Importing Executables
---------------------

To start, we will walk through a simple example that creates an
:prop_tgt:`IMPORTED` executable target and then references it from the
:command:`add_custom_command` command.

We'll need to do some setup to get started. We want to create an executable
that when run creates a basic ``main.cc`` file in the current directory. The
details of this project are not important. Navigate to
``Help/guide/importing-exporting/MyExe``, create a build directory, run
:manual:`cmake <cmake(1)>` and build and install the project.

.. code-block:: console

  $ cd Help/guide/importing-exporting/MyExe
  $ mkdir build
  $ cd build
  $ cmake ..
  $ cmake --build .
  $ cmake --install . --prefix <install location>
  $ <install location>/myexe
  $ ls
  [...] main.cc [...]

Now we can import this executable into another CMake project. The source code
for this section is available in ``Help/guide/importing-exporting/Importing``.
In the CMakeLists file, use the :command:`add_executable` command to create a
new target called ``myexe``. Use the ``IMPORTED`` option to tell CMake that
this target references an executable file located outside of the project. No
rules will be generated to build it and the :prop_tgt:`IMPORTED` target
property will be set  to ``true``.

.. literalinclude:: Importing/CMakeLists.txt
  :language: cmake
  :start-after: # Add executable
  :end-before: # Set imported location

Next, set the :prop_tgt:`IMPORTED_LOCATION` property of the target using
the :command:`set_property` command. This will tell CMake the location of the
target on disk. The location may need to be adjusted to the
``<install location>`` specified in the previous step.

.. literalinclude:: Importing/CMakeLists.txt
  :language: cmake
  :start-after: # Set imported location
  :end-before: # Add custom command

We can now reference this :prop_tgt:`IMPORTED` target just like any target
built within the project. In this instance, let's imagine that we want to use
the generated source file in our project. Use the :prop_tgt:`IMPORTED`
target in the :command:`add_custom_command` command:

.. literalinclude:: Importing/CMakeLists.txt
  :language: cmake
  :start-after: # Add custom command
  :end-before: # Use source file

As ``COMMAND`` specifies an executable target name, it will automatically be
replaced by the location of the executable given by the
:prop_tgt:`IMPORTED_LOCATION` property above.

Finally, use the output from :command:`add_custom_command`:

.. literalinclude:: Importing/CMakeLists.txt
  :language: cmake
  :start-after: # Use source file

Importing Libraries
-------------------

In a similar manner, libraries from other projects may be accessed through
:prop_tgt:`IMPORTED` targets.

Note: The full source code for the examples in this section is not provided
and is left as an exercise for the reader.

In the CMakeLists file, add an :prop_tgt:`IMPORTED` library and specify its
location on disk:

.. code-block:: cmake

  add_library(foo STATIC IMPORTED)
  set_property(TARGET foo PROPERTY
               IMPORTED_LOCATION "/path/to/libfoo.a")

Then use the :prop_tgt:`IMPORTED` library inside of our project:

.. code-block:: cmake

  add_executable(myexe src1.c src2.c)
  target_link_libraries(myexe PRIVATE foo)


On Windows, a .dll and its .lib import library may be imported together:

.. code-block:: cmake

  add_library(bar SHARED IMPORTED)
  set_property(TARGET bar PROPERTY
               IMPORTED_LOCATION "c:/path/to/bar.dll")
  set_property(TARGET bar PROPERTY
               IMPORTED_IMPLIB "c:/path/to/bar.lib")
  add_executable(myexe src1.c src2.c)
  target_link_libraries(myexe PRIVATE bar)

A library with multiple configurations may be imported with a single target:

.. code-block:: cmake

  find_library(math_REL NAMES m)
  find_library(math_DBG NAMES md)
  add_library(math STATIC IMPORTED GLOBAL)
  set_target_properties(math PROPERTIES
    IMPORTED_LOCATION "${math_REL}"
    IMPORTED_LOCATION_DEBUG "${math_DBG}"
    IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
  )
  add_executable(myexe src1.c src2.c)
  target_link_libraries(myexe PRIVATE math)

The generated build system will link ``myexe`` to ``m.lib`` when built in the
release configuration, and ``md.lib`` when built in the debug configuration.

Exporting Targets
=================

While :prop_tgt:`IMPORTED` targets on their own are useful, they still
require that the project that imports them knows the locations of the target
files on disk. The real power of :prop_tgt:`IMPORTED`  targets is when the
project providing the target files also provides a CMake file to help import
them. A project can be setup to produce the necessary information so that it
can easily be used by other CMake projects be it from a build directory, a
local install or when packaged.

In the remaining sections, we will walk through a set of example projects
step-by-step. The first project will create and install a library and
corresponding CMake configuration and package files. The second project will
use the generated package.

Let's start by looking at the ``MathFunctions`` project in the
``Help/guide/importing-exporting/MathFunctions`` directory. Here we have a
header file ``MathFunctions.h`` that declares a ``sqrt`` function:

.. literalinclude:: MathFunctions/MathFunctions.h
  :language: c++

And a corresponding source file ``MathFunctions.cxx``:

.. literalinclude:: MathFunctions/MathFunctions.cxx
  :language: c++

Don't worry too much about the specifics of the C++ files, they are just meant
to be a simple example that will compile and run on many build systems.

Now we can create a ``CMakeLists.txt`` file for the ``MathFunctions``
project. Start by specifying the :command:`cmake_minimum_required` version and
:command:`project` name:

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :end-before: # create library

Create a library called ``MathFunctions`` with the :command:`add_library`
command:

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # create library
  :end-before: # add include directories

And then use the :command:`target_include_directories` command to specify the
include directories for the target:

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # add include directories
  :end-before: # install the target and create export-set

We need to tell CMake that we want to use different include directories
depending on if we're building the library or using it from an installed
location. If we don't do this, when CMake creates the export information it
will export a path that is specific to the current build directory
and will not be valid for other projects. We can use
:manual:`generator expressions <cmake-generator-expressions(7)>` to specify
that if we're building the library include the current source directory.
Otherwise, when installed, include the ``include`` directory. See the `Creating
Relocatable Packages`_ section for more details.

The :command:`install(TARGETS)` and :command:`install(EXPORT)` commands
work together to install both targets (a library in our case) and a CMake
file designed to make it easy to import the targets into another CMake project.

First, in the :command:`install(TARGETS)` command we will specify the target,
the ``EXPORT`` name and the destinations that tell CMake where to install the
targets.

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # install the target and create export-set
  :end-before: # install header file

Here, the ``EXPORT`` option tells CMake to create an export called
``MathFunctionsTargets``. The generated :prop_tgt:`IMPORTED` targets have
appropriate properties set to define their
:ref:`usage requirements <Target Usage Requirements>`, such as
:prop_tgt:`INTERFACE_INCLUDE_DIRECTORIES`,
:prop_tgt:`INTERFACE_COMPILE_DEFINITIONS` and other relevant built-in
``INTERFACE_`` properties.  The ``INTERFACE`` variant of user-defined
properties listed in :prop_tgt:`COMPATIBLE_INTERFACE_STRING` and other
:ref:`Compatible Interface Properties` are also propagated to the
generated :prop_tgt:`IMPORTED` targets. For example, in this case, the
:prop_tgt:`IMPORTED` target will have its
:prop_tgt:`INTERFACE_INCLUDE_DIRECTORIES` property populated with
the directory specified by the ``INCLUDES DESTINATION`` property. As a
relative path was given, it is treated as relative to the
:variable:`CMAKE_INSTALL_PREFIX`.

Note, we have *not* asked CMake to install the export yet.

We don't want to forget to install the ``MathFunctions.h`` header file with the
:command:`install(FILES)` command. The header file should be installed to the
``include`` directory, as specified by the
:command:`target_include_directories` command above.

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # install header file
  :end-before: # generate and install export file

Now that the ``MathFunctions`` library and header file are installed, we also
need to explicitly install the ``MathFunctionsTargets``  export details. Use
the :command:`install(EXPORT)` command to export the targets in
``MathFunctionsTargets``, as defined by the  :command:`install(TARGETS)`
command.

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # generate and install export file
  :end-before: # include CMakePackageConfigHelpers macro

This command generates the ``MathFunctionsTargets.cmake`` file and arranges
to install it to ``lib/cmake``. The file contains code suitable for
use by downstreams to import all targets listed in the install command from
the installation tree.

The ``NAMESPACE`` option will prepend ``MathFunctions::`` to  the target names
as they are written to the export file. This convention of double-colons
gives CMake a hint that the name is an  :prop_tgt:`IMPORTED` target when it
is used by downstream projects. This way, CMake can issue a diagnostic
message if the package providing it was not found.

The generated export file contains code that creates an :prop_tgt:`IMPORTED` library.

.. code-block:: cmake

  # Create imported target MathFunctions::MathFunctions
  add_library(MathFunctions::MathFunctions STATIC IMPORTED)

  set_target_properties(MathFunctions::MathFunctions PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include"
  )

This code is very similar to the example we created by hand in the
`Importing Libraries`_ section. Note that ``${_IMPORT_PREFIX}`` is computed
relative to the file location.

An outside project may load this file with the :command:`include` command and
reference the ``MathFunctions`` library from the installation tree as if it
were built in its own tree. For example:

.. code-block:: cmake
  :linenos:

   include(${INSTALL_PREFIX}/lib/cmake/MathFunctionTargets.cmake)
   add_executable(myexe src1.c src2.c )
   target_link_libraries(myexe PRIVATE MathFunctions::MathFunctions)

Line 1 loads the target CMake file. Although we only exported a single
target, this file may import any number of targets. Their locations are
computed relative to the file location so that the install tree may be
easily moved. Line 3 references the imported ``MathFunctions`` library. The
resulting build system will link to the library from its installed location.

Executables may also be exported and imported using the same process.

Any number of target installations may be associated with the same
export name. Export names are considered global so any directory may
contribute a target installation. The :command:`install(EXPORT)` command only
needs to be called once to install a file that references all targets. Below
is an example of how multiple exports may be combined into a single
export file, even if they are in different subdirectories of the project.

.. code-block:: cmake

  # A/CMakeLists.txt
  add_executable(myexe src1.c)
  install(TARGETS myexe DESTINATION lib/myproj
          EXPORT myproj-targets)

  # B/CMakeLists.txt
  add_library(foo STATIC foo1.c)
  install(TARGETS foo DESTINATION lib EXPORTS myproj-targets)

  # Top CMakeLists.txt
  add_subdirectory (A)
  add_subdirectory (B)
  install(EXPORT myproj-targets DESTINATION lib/myproj)

Creating Packages
-----------------

At this point, the ``MathFunctions`` project is exporting the target
information required to be used by other projects. We can make this project
even easier for other projects to use by generating a configuration file so
that the CMake :command:`find_package` command can find our project.

To start, we will need to make a few additions to the ``CMakeLists.txt``
file. First, include the :module:`CMakePackageConfigHelpers` module to get
access to some helper functions for creating config files.

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # include CMakePackageConfigHelpers macro
  :end-before: # set version

Then we will create a package configuration file and a package version file.

Creating a Package Configuration File
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use the :command:`configure_package_config_file` command provided by the
:module:`CMakePackageConfigHelpers` to generate the package configuration
file. Note that this command should be used instead of the plain
:command:`configure_file` command. It helps to ensure that the resulting
package is relocatable by avoiding hardcoded paths in the installed
configuration file. The path given to ``INSTALL_DESTINATION`` must  be the
destination where the ``MathFunctionsConfig.cmake`` file will be installed.
We will examine the contents of the package configuration file in the next
section.

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # create config file
  :end-before: # install config files

Install the generated configuration files with the :command:`INSTALL(files)`
command. Both ``MathFunctionsConfigVersion.cmake`` and
``MathFunctionsConfig.cmake`` are installed to the same location, completing
the package.

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # install config files
  :end-before: # generate the export targets for the build tree

Now we need to create the package configuration file itself. In this case, the
``Config.cmake.in`` file is very simple but sufficient to allow downstreams
to use the :prop_tgt:`IMPORTED` targets.

.. literalinclude:: MathFunctions/Config.cmake.in

The first line of the file contains only the string ``@PACKAGE_INIT@``. This
expands when the file is configured and allows the use of relocatable paths
prefixed with ``PACKAGE_``. It also provides the ``set_and_check()`` and
``check_required_components()`` macros.

The ``check_required_components`` helper macro ensures that all requested,
non-optional components have been found by checking the
``<Package>_<Component>_FOUND`` variables for all required components. This
macro should be called at the end of the package configuration file even if the
package does not have any components. This way, CMake can make sure that the
downstream project hasn't specified any non-existent components. If
``check_required_components`` fails, the ``<Package>_FOUND`` variable is set to
FALSE, and the package is considered to be not found.

The ``set_and_check()`` macro should be used in configuration files instead
of the normal ``set()`` command for setting directories and file locations.
If a referenced file or directory does not exist, the macro will fail.

If any macros should be provided by the ``MathFunctions`` package, they should
be in a separate file which is installed to the same location as the
``MathFunctionsConfig.cmake`` file, and included from there.

**All required dependencies of a package must also be found in the package
configuration file.** Let's imagine that we require the ``Stats`` library in
our project. In the CMakeLists file, we would add:

.. code-block:: cmake

  find_package(Stats 2.6.4 REQUIRED)
  target_link_libraries(MathFunctions PUBLIC Stats::Types)

As the ``Stats::Types`` target is a ``PUBLIC`` dependency of ``MathFunctions``,
downstreams must also find the ``Stats`` package and link to the
``Stats::Types`` library.  The ``Stats`` package should be found in the
configuration file to ensure this.

.. code-block:: cmake

  include(CMakeFindDependencyMacro)
  find_dependency(Stats 2.6.4)

The ``find_dependency`` macro from the :module:`CMakeFindDependencyMacro`
module helps by propagating  whether the package is ``REQUIRED``, or
``QUIET``, etc. The ``find_dependency`` macro also sets
``MathFunctions_FOUND`` to ``False`` if the dependency is not found, along
with a diagnostic that the ``MathFunctions`` package cannot be used without
the ``Stats`` package.

**Exercise:** Add a required library to the ``MathFunctions`` project.

Creating a Package Version File
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The :module:`CMakePackageConfigHelpers` module provides the
:command:`write_basic_package_version_file` command for creating a simple
package version file.  This file is read by CMake when :command:`find_package`
is called to determine the compatibility with the requested version, and to set
some version-specific variables such as ``<PackageName>_VERSION``,
``<PackageName>_VERSION_MAJOR``, ``<PackageName>_VERSION_MINOR``, etc. See
:manual:`cmake-packages <cmake-packages(7)>` documentation for more details.

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # set version
  :end-before: # create config file

In our example, ``MathFunctions_MAJOR_VERSION`` is defined as a
:prop_tgt:`COMPATIBLE_INTERFACE_STRING` which means that it must be
compatible among the dependencies of any depender. By setting this
custom defined user property in this version and in the next version of
``MathFunctions``, :manual:`cmake <cmake(1)>` will issue a diagnostic if
there is an attempt to use version 3 together with version 4.  Packages can
choose to employ such a pattern if different major versions of the package
are designed to be incompatible.


Exporting Targets from the Build Tree
-------------------------------------

Typically, projects are built and installed before being used by an outside
project. However, in some cases, it is desirable to export targets directly
from a build tree. The targets may then be used by an outside project that
references the build tree with no installation involved. The :command:`export`
command is used to generate a file exporting targets from a project build tree.

If we want our example project to also be used from a build directory we only
have to add the following to ``CMakeLists.txt``:

.. literalinclude:: MathFunctions/CMakeLists.txt
  :language: cmake
  :start-after: # generate the export targets for the build tree

Here we use the :command:`export` command to generate the export targets for
the build tree. In this case, we'll create a file called
``MathFunctionsTargets.cmake`` in the ``cmake`` subdirectory of the build
directory. The generated file contains the required code to import the target
and may be loaded by an outside project that is aware of the project build
tree. This file is specific to the build-tree, and **is not relocatable**.

It is possible to create a suitable package configuration file and package
version file to define a package for the build tree which may be used without
installation.  Consumers of the build tree can simply ensure that the
:variable:`CMAKE_PREFIX_PATH` contains the build directory, or set the
``MathFunctions_DIR`` to ``<build_dir>/MathFunctions`` in the cache.

An example application of this feature is for building an executable on a host
platform when cross-compiling. The project containing the executable may be
built on the host platform and then the project that is being cross-compiled
for another platform may load it.

Building and Installing a Package
---------------------------------

At this point, we have generated a relocatable CMake configuration for our
project that can be used after the project has been installed. Let's try to
build the ``MathFunctions`` project:

.. code-block:: console

  mkdir MathFunctions_build
  cd MathFunctions_build
  cmake ../MathFunctions
  cmake --build .

In the build directory, notice that the file ``MathFunctionsTargets.cmake``
has been created in the ``cmake`` subdirectory.

Now install the project:

.. code-block:: console

    $ cmake --install . --prefix "/home/myuser/installdir"

Creating Relocatable Packages
=============================

Packages created by :command:`install(EXPORT)` are designed to be relocatable,
using paths relative to the location of the package itself. They must not
reference absolute paths of files on the machine where the package is built
that will not exist on the machines where the package may be installed.

When defining the interface of a target for ``EXPORT``, keep in mind that the
include directories should be specified as relative paths to the
:variable:`CMAKE_INSTALL_PREFIX` but should not explicitly include the
:variable:`CMAKE_INSTALL_PREFIX`:

.. code-block:: cmake

  target_include_directories(tgt INTERFACE
    # Wrong, not relocatable:
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}/include/TgtName>
  )

  target_include_directories(tgt INTERFACE
    # Ok, relocatable:
    $<INSTALL_INTERFACE:include/TgtName>
  )

The ``$<INSTALL_PREFIX>``
:manual:`generator expression <cmake-generator-expressions(7)>` may be used as
a placeholder for the install prefix without resulting in a non-relocatable
package.  This is necessary if complex generator expressions are used:

.. code-block:: cmake

  target_include_directories(tgt INTERFACE
    # Ok, relocatable:
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/TgtName>
  )

This also applies to paths referencing external dependencies.
It is not advisable to populate any properties which may contain
paths, such as :prop_tgt:`INTERFACE_INCLUDE_DIRECTORIES` or
:prop_tgt:`INTERFACE_LINK_LIBRARIES`, with paths relevant to dependencies.
For example, this code may not work well for a relocatable package:

.. code-block:: cmake

  target_link_libraries(MathFunctions INTERFACE
    ${Foo_LIBRARIES} ${Bar_LIBRARIES}
    )
  target_include_directories(MathFunctions INTERFACE
    "$<INSTALL_INTERFACE:${Foo_INCLUDE_DIRS};${Bar_INCLUDE_DIRS}>"
    )

The referenced variables may contain the absolute paths to libraries
and include directories **as found on the machine the package was made on**.
This would create a package with hard-coded paths to dependencies not
suitable for relocation.

Ideally such dependencies should be used through their own
:ref:`IMPORTED targets <Imported Targets>` that have their own
:prop_tgt:`IMPORTED_LOCATION` and usage requirement properties
such as :prop_tgt:`INTERFACE_INCLUDE_DIRECTORIES` populated
appropriately.  Those imported targets may then be used with
the :command:`target_link_libraries` command for ``MathFunctions``:

.. code-block:: cmake

  target_link_libraries(MathFunctions INTERFACE Foo::Foo Bar::Bar)

With this approach the package references its external dependencies
only through the names of :ref:`IMPORTED targets <Imported Targets>`.
When a consumer uses the installed package, the consumer will run the
appropriate :command:`find_package` commands (via the ``find_dependency``
macro described above) to find the dependencies and populate the
imported targets with appropriate paths on their own machine.

Using the Package Configuration File
====================================

Now we're ready to create a project to use the installed ``MathFunctions``
library. In this section we will be using source code from
``Help\guide\importing-exporting\Downstream``. In this directory, there is a
source file called ``main.cc`` that uses the ``MathFunctions`` library to
calculate the square root of a given number and then prints the results:

.. literalinclude:: Downstream/main.cc
  :language: c++

As before, we'll start with the :command:`cmake_minimum_required` and
:command:`project` commands in the ``CMakeLists.txt`` file. For this project,
we'll also specify the C++ standard.

.. literalinclude:: Downstream/CMakeLists.txt
  :language: cmake
  :end-before: # find MathFunctions

We can use the :command:`find_package` command:

.. literalinclude:: Downstream/CMakeLists.txt
  :language: cmake
  :start-after: # find MathFunctions
  :end-before: # create executable

Create an exectuable:

.. literalinclude:: Downstream/CMakeLists.txt
  :language: cmake
  :start-after: # create executable
  :end-before: # use MathFunctions library

And link to the ``MathFunctions`` library:

.. literalinclude:: Downstream/CMakeLists.txt
  :language: cmake
  :start-after: # use MathFunctions library

That's it! Now let's try to build the ``Downstream`` project.

.. code-block:: console

  mkdir Downstream_build
  cd Downstream_build
  cmake ../Downstream
  cmake --build .

A warning may have appeared during CMake configuration:

.. code-block:: console

  CMake Warning at CMakeLists.txt:4 (find_package):
    By not providing "FindMathFunctions.cmake" in CMAKE_MODULE_PATH this
    project has asked CMake to find a package configuration file provided by
    "MathFunctions", but CMake did not find one.

    Could not find a package configuration file provided by "MathFunctions"
    with any of the following names:

      MathFunctionsConfig.cmake
      mathfunctions-config.cmake

    Add the installation prefix of "MathFunctions" to CMAKE_PREFIX_PATH or set
    "MathFunctions_DIR" to a directory containing one of the above files.  If
    "MathFunctions" provides a separate development package or SDK, be sure it
    has been installed.

Set the ``CMAKE_PREFIX_PATH`` to where MathFunctions was installed previously
and try again. Ensure that the newly created executable runs as expected.

Adding Components
=================

Let's edit the ``MathFunctions`` project to use components. The source code for
this section can be found in
``Help\guide\importing-exporting\MathFunctionsComponents``. The CMakeLists file
for this project adds two subdirectories: ``Addition`` and ``SquareRoot``.

.. literalinclude:: MathFunctionsComponents/CMakeLists.txt
  :language: cmake
  :end-before: # include CMakePackageConfigHelpers macro

Generate and install the package configuration and package version files:

.. literalinclude:: MathFunctionsComponents/CMakeLists.txt
  :language: cmake
  :start-after: # include CMakePackageConfigHelpers macro

If ``COMPONENTS`` are specified when the downstream uses
:command:`find_package`, they are listed in the
``<PackageName>_FIND_COMPONENTS`` variable. We can use this variable to verify
that all necessary component targets are included in ``Config.cmake.in``. At
the same time, this function will act as a custom ``check_required_components``
macro to ensure that the downstream only attempts to use supported components.

.. literalinclude:: MathFunctionsComponents/Config.cmake.in

Here, the ``MathFunctions_NOT_FOUND_MESSAGE`` is set to a diagnosis that the
package could not be found because an invalid component was specified. This
message variable can be set for any case where the ``_FOUND`` variable is set
to ``False``, and will be displayed to the user.

The ``Addition`` and ``SquareRoot`` directories are similar. Let's look at one
of the CMakeLists files:

.. literalinclude:: MathFunctionsComponents/SquareRoot/CMakeLists.txt
  :language: cmake

Now we can build the project as described in earlier sections. To test using
this package, we can use the project in
``Help\guide\importing-exporting\DownstreamComponents``. There's two
differences from the previous ``Downstream`` project. First, we need to find
the package components. Change the ``find_package`` line from:

.. literalinclude:: Downstream/CMakeLists.txt
  :language: cmake
  :start-after: # find MathFunctions
  :end-before: # create executable

To:

.. literalinclude:: DownstreamComponents/CMakeLists.txt
  :language: cmake
  :start-after: # find MathFunctions
  :end-before: # create executable

and the ``target_link_libraries`` line from:

.. literalinclude:: Downstream/CMakeLists.txt
  :language: cmake
  :start-after: # use MathFunctions library

To:

.. literalinclude:: DownstreamComponents/CMakeLists.txt
  :language: cmake
  :start-after: # use MathFunctions library
  :end-before: # Workaround for GCC on AIX to avoid -isystem

In ``main.cc``, replace ``#include MathFunctions.h`` with:

.. literalinclude:: DownstreamComponents/main.cc
  :language: c
  :start-after: #include <string>
  :end-before: int main

Finally, use the ``Addition`` library:

.. literalinclude:: DownstreamComponents/main.cc
  :language: c
  :start-after: // calculate sum
  :end-before: return 0;

Build the ``Downstream`` project and confirm that it can find and use the
package components.
