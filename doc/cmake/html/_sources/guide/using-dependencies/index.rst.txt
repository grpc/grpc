Using Dependencies Guide
************************

.. only:: html

   .. contents::

Introduction
============

For developers wishing to use CMake to consume a third
party binary package, there are multiple possibilities
regarding how to optimally do so, depending on how
CMake-aware the third-party library is.

CMake files provided with a software package contain
instructions for finding each build dependency.  Some
build dependencies are optional in that the build may
succeed with a different feature set if the dependency
is missing, and some dependencies are required.  CMake
searches well-known locations for each dependency, and
the provided software may supply additional hints or
locations to CMake to find each dependency.

If a required dependency is not found by
:manual:`cmake(1)`, the cache is populated with an entry
which contains a ``NOTFOUND`` value.  This value can be
replaced by specifying it on the command line, or in
the :manual:`ccmake(1)` or :manual:`cmake-gui(1)` tool.
See the :guide:`User Interaction Guide` for
more about setting cache entries.

Libraries providing Config-file packages
========================================

The most convenient way for a third-party to provide library
binaries for use with CMake is to provide
:ref:`Config File Packages`.  These packages are text files
shipped with the library which instruct CMake how to use the
library binaries and associated headers, helper tools and
CMake macros provided by the library.

The config files can usually be found in a directory whose
name matches the pattern ``lib/cmake/<PackageName>``, though
they may be in other locations instead.  The
``<PackageName>`` corresponds to use in CMake code with the
:command:`find_package` command such as
``find_package(PackageName REQUIRED)``.

The ``lib/cmake/<PackageName>`` directory will contain a
file which is either named ``<PackageName>Config.cmake``
or ``<PackageName>-config.cmake``.  This is the entry point
to the package for CMake.  A separate optional file named
``<PackageName>ConfigVersion.cmake`` may also exist in the
directory.  This file is used by CMake to determine whether
the version of the third party package satisfies uses of the
:command:`find_package` command which specify version
constraints.  It is optional to specify a version when using
:command:`find_package`, even if a ``ConfigVersion`` file is
present.

If the ``Config.cmake`` file is found and the
optionally-specified version is satisfied, then the CMake
:command:`find_package` command considers the package to be
found and the entire library package is assumed to be
complete as designed.

There may be additional files providing CMake macros or
:ref:`imported targets` for you to use.  CMake does not
enforce any naming convention for these
files.  They are related to the primary ``Config`` file by
use of the CMake :command:`include` command.

:guide:`Invoking CMake <User Interaction Guide>` with the
intent of using a package of third party binaries requires
that cmake :command:`find_package` commands succeed in finding
the package.  If the location of the package is in a directory
known to CMake, the :command:`find_package` call should
succeed.  The directories known to cmake are platform-specific.
For example, packages installed on Linux with a standard
system package manager will be found in the ``/usr`` prefix
automatically.  Packages installed in ``Program Files`` on
Windows will similarly be found automatically.

Packages which are not found automatically are in locations
not predictable to CMake such as ``/opt/mylib`` or
``$HOME/dev/prefix``.  This is a normal situation and CMake
provides several ways for users to specify where to find
such libraries.

The :variable:`CMAKE_PREFIX_PATH` variable may be
:ref:`set when invoking CMake <Setting Build Variables>`.
It is treated as a list of paths to search for
:ref:`Config File Packages`.  A package installed in
``/opt/somepackage`` will typically install config files
such as
``/opt/somepackage/lib/cmake/somePackage/SomePackageConfig.cmake``.
In that case, ``/opt/somepackage`` should be added to
:variable:`CMAKE_PREFIX_PATH`.

The environment variable ``CMAKE_PREFIX_PATH`` may also be
populated with prefixes to search for packages.  Like the
``PATH`` environment variable, this is a list and needs to use
the platform-specific environment variable list item separator
(``:`` on Unix and ``;`` on Windows).

The :variable:`CMAKE_PREFIX_PATH` variable provides convenience
in cases where multiple prefixes need to be specified, or when
multiple different package binaries are available in the same
prefix.  Paths to packages may also be specified by setting
variables matching ``<PackageName>_DIR``, such as
``SomePackage_DIR``.  Note that this is not a prefix but should
be a full path to a directory containing a config-style package
file, such as ``/opt/somepackage/lib/cmake/SomePackage/`` in
the above example.

Imported Targets from Packages
==============================

A third-party package which provides config-file packages may
also provide :ref:`Imported targets`. These will be
specified in files containing configuration-specific file
paths relevant to the package, such as debug and release
versions of libraries.

Often the third-party package documentation will point out the
names of imported targets available after a successful
``find_package`` for a library.  Those imported target names
can be used with the :command:`target_link_libraries` command.

A complete example which makes a simple use of a third party
library might look like:

.. code-block:: cmake

    cmake_minimum_required(VERSION 3.10)
    project(MyExeProject VERSION 1.0.0)

    find_package(SomePackage REQUIRED)
    add_executable(MyExe main.cpp)
    target_link_libraries(MyExe PRIVATE SomePrefix::LibName)

See :manual:`cmake-buildsystem(7)` for further information
about developing a CMake buildsystem.

Libraries not Providing Config-file Packages
--------------------------------------------

Third-party libraries which do not provide config-file packages
can still be found with the :command:`find_package` command, if
a ``FindSomePackage.cmake`` file is available.

These module-file packages are different to config-file packages
in that:

#. They should not be provided by the third party, except
   perhaps in the form of documentation
#. The availability of a ``Find<PackageName>.cmake`` file does
   not indicate the availability of the binaries themselves.
#. CMake does not search the :variable:`CMAKE_PREFIX_PATH` for
   ``Find<PackageName>.cmake`` files.  Instead CMake searches
   for such files in the :variable:`CMAKE_MODULE_PATH`
   variable. It is common for users to set the
   :variable:`CMAKE_MODULE_PATH` when running CMake, and it is
   common for CMake projects to append to
   :variable:`CMAKE_MODULE_PATH` to allow use of local
   module-file packages.
#. CMake ships ``Find<PackageName>.cmake`` files for some
   :manual:`third party packages <cmake-modules(7)>`
   for convenience in cases where the third party does
   not provide config-file packages directly.  These files are
   a maintenance burden for CMake, so new Find modules are
   generally not added to CMake anymore.  Third-parties should
   provide config file packages instead of relying on a Find
   module to be provided by CMake.

Module-file packages may also provide :ref:`Imported targets`.
A complete example which finds such a package might look
like:

.. code-block:: cmake

    cmake_minimum_required(VERSION 3.10)
    project(MyExeProject VERSION 1.0.0)

    find_package(PNG REQUIRED)

    # Add path to a FindSomePackage.cmake file
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
    find_package(SomePackage REQUIRED)

    add_executable(MyExe main.cpp)
    target_link_libraries(MyExe PRIVATE
        PNG::PNG
        SomePrefix::LibName
    )

The :variable:`<PackageName>_ROOT` variable is also
searched as a prefix for :command:`find_package` calls using
module-file packages such as ``FindSomePackage``.
