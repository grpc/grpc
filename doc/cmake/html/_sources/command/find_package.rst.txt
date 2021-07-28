find_package
------------

.. only:: html

   .. contents::

Find an external project, and load its settings.

.. _`basic signature`:

Basic Signature and Module Mode
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  find_package(<PackageName> [version] [EXACT] [QUIET] [MODULE]
               [REQUIRED] [[COMPONENTS] [components...]]
               [OPTIONAL_COMPONENTS components...]
               [NO_POLICY_SCOPE])

Finds and loads settings from an external project.  ``<PackageName>_FOUND``
will be set to indicate whether the package was found.  When the
package is found package-specific information is provided through
variables and :ref:`Imported Targets` documented by the package itself.  The
``QUIET`` option disables informational messages, including those indicating
that the package cannot be found if it is not ``REQUIRED``.  The ``REQUIRED``
option stops processing with an error message if the package cannot be found.

A package-specific list of required components may be listed after the
``COMPONENTS`` option (or after the ``REQUIRED`` option if present).
Additional optional components may be listed after
``OPTIONAL_COMPONENTS``.  Available components and their influence on
whether a package is considered to be found are defined by the target
package.

.. _FIND_PACKAGE_VERSION_FORMAT:

The ``[version]`` argument requests a version with which the package found
should be compatible. There are two possible forms in which it may be
specified:

  * A single version with the format ``major[.minor[.patch[.tweak]]]``.
  * A version range with the format ``versionMin...[<]versionMax`` where
    ``versionMin`` and ``versionMax`` have the same format as the single
    version.  By default, both end points are included.  By specifying ``<``,
    the upper end point will be excluded.  Version ranges are only supported
    with CMake 3.19 or later.

The ``EXACT`` option requests that the version be matched exactly. This option
is incompatible with the specification of a version range.

If no ``[version]`` and/or component list is given to a recursive invocation
inside a find-module, the corresponding arguments are forwarded
automatically from the outer call (including the ``EXACT`` flag for
``[version]``).  Version support is currently provided only on a
package-by-package basis (see the `Version Selection`_ section below).
When a version range is specified but the package is only designed to expect
a single version, the package will ignore the upper end point of the range and
only take the single version at the lower end of the range into account.

See the :command:`cmake_policy` command documentation for discussion
of the ``NO_POLICY_SCOPE`` option.

The command has two modes by which it searches for packages: "Module"
mode and "Config" mode.  The above signature selects Module mode.
If no module is found the command falls back to Config mode, described
below. This fall back is disabled if the ``MODULE`` option is given.

In Module mode, CMake searches for a file called ``Find<PackageName>.cmake``.
The file is first searched in the :variable:`CMAKE_MODULE_PATH`,
then among the :ref:`Find Modules` provided by the CMake installation.
If the file is found, it is read and processed by CMake.  It is responsible
for finding the package, checking the version, and producing any needed
messages.  Some find-modules provide limited or no support for versioning;
check the module documentation.

If the ``MODULE`` option is not specified in the above signature,
CMake first searches for the package using Module mode. Then, if the
package is not found, it searches again using Config mode. A user
may set the variable :variable:`CMAKE_FIND_PACKAGE_PREFER_CONFIG` to
``TRUE`` to direct CMake first search using Config mode before falling
back to Module mode.

Full Signature and Config Mode
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

User code should generally look for packages using the above `basic
signature`_.  The remainder of this command documentation specifies the
full command signature and details of the search process.  Project
maintainers wishing to provide a package to be found by this command
are encouraged to read on.

The complete Config mode command signature is

.. code-block:: cmake

  find_package(<PackageName> [version] [EXACT] [QUIET]
               [REQUIRED] [[COMPONENTS] [components...]]
               [OPTIONAL_COMPONENTS components...]
               [CONFIG|NO_MODULE]
               [NO_POLICY_SCOPE]
               [NAMES name1 [name2 ...]]
               [CONFIGS config1 [config2 ...]]
               [HINTS path1 [path2 ... ]]
               [PATHS path1 [path2 ... ]]
               [PATH_SUFFIXES suffix1 [suffix2 ...]]
               [NO_DEFAULT_PATH]
               [NO_PACKAGE_ROOT_PATH]
               [NO_CMAKE_PATH]
               [NO_CMAKE_ENVIRONMENT_PATH]
               [NO_SYSTEM_ENVIRONMENT_PATH]
               [NO_CMAKE_PACKAGE_REGISTRY]
               [NO_CMAKE_BUILDS_PATH] # Deprecated; does nothing.
               [NO_CMAKE_SYSTEM_PATH]
               [NO_CMAKE_SYSTEM_PACKAGE_REGISTRY]
               [CMAKE_FIND_ROOT_PATH_BOTH |
                ONLY_CMAKE_FIND_ROOT_PATH |
                NO_CMAKE_FIND_ROOT_PATH])

The ``CONFIG`` option, the synonymous ``NO_MODULE`` option, or the use
of options not specified in the `basic signature`_ all enforce pure Config
mode.  In pure Config mode, the command skips Module mode search and
proceeds at once with Config mode search.

Config mode search attempts to locate a configuration file provided by the
package to be found.  A cache entry called ``<PackageName>_DIR`` is created to
hold the directory containing the file.  By default the command
searches for a package with the name ``<PackageName>``.  If the ``NAMES`` option
is given the names following it are used instead of ``<PackageName>``.
The command searches for a file called ``<PackageName>Config.cmake`` or
``<lower-case-package-name>-config.cmake`` for each name specified.
A replacement set of possible configuration file names may be given
using the ``CONFIGS`` option.  The search procedure is specified below.
Once found, the configuration file is read and processed by CMake.
Since the file is provided by the package it already knows the
location of package contents.  The full path to the configuration file
is stored in the cmake variable ``<PackageName>_CONFIG``.

All configuration files which have been considered by CMake while
searching for an installation of the package with an appropriate
version are stored in the cmake variable ``<PackageName>_CONSIDERED_CONFIGS``,
the associated versions in ``<PackageName>_CONSIDERED_VERSIONS``.

If the package configuration file cannot be found CMake will generate
an error describing the problem unless the ``QUIET`` argument is
specified.  If ``REQUIRED`` is specified and the package is not found a
fatal error is generated and the configure step stops executing.  If
``<PackageName>_DIR`` has been set to a directory not containing a
configuration file CMake will ignore it and search from scratch.

Package maintainers providing CMake package configuration files are
encouraged to name and install them such that the `Search Procedure`_
outlined below will find them without requiring use of additional options.

Version Selection
^^^^^^^^^^^^^^^^^

When the ``[version]`` argument is given, Config mode will only find a
version of the package that claims compatibility with the requested
version (see :ref:`format specification <FIND_PACKAGE_VERSION_FORMAT>`). If the
``EXACT`` option is given, only a version of the package claiming an exact match
of the requested version may be found.  CMake does not establish any
convention for the meaning of version numbers.  Package version
numbers are checked by "version" files provided by the packages
themselves.  For a candidate package configuration file
``<config-file>.cmake`` the corresponding version file is located next
to it and named either ``<config-file>-version.cmake`` or
``<config-file>Version.cmake``.  If no such version file is available
then the configuration file is assumed to not be compatible with any
requested version.  A basic version file containing generic version
matching code can be created using the
:module:`CMakePackageConfigHelpers` module.  When a version file
is found it is loaded to check the requested version number.  The
version file is loaded in a nested scope in which the following
variables have been defined:

``PACKAGE_FIND_NAME``
  The ``<PackageName>``
``PACKAGE_FIND_VERSION``
  Full requested version string
``PACKAGE_FIND_VERSION_MAJOR``
  Major version if requested, else 0
``PACKAGE_FIND_VERSION_MINOR``
  Minor version if requested, else 0
``PACKAGE_FIND_VERSION_PATCH``
  Patch version if requested, else 0
``PACKAGE_FIND_VERSION_TWEAK``
  Tweak version if requested, else 0
``PACKAGE_FIND_VERSION_COUNT``
  Number of version components, 0 to 4

When a version range is specified, the above version variables will hold
values based on the lower end of the version range.  This is to preserve
compatibility with packages that have not been implemented to expect version
ranges.  In addition, the version range will be described by the following
variables:

``PACKAGE_FIND_VERSION_RANGE``
  Full requested version range string
``PACKAGE_FIND_VERSION_RANGE_MIN``
  This specifies whether the lower end point of the version range should be
  included or excluded.  Currently, the only supported value for this variable
  is ``INCLUDE``.
``PACKAGE_FIND_VERSION_RANGE_MAX``
  This specifies whether the upper end point of the version range should be
  included or excluded.  The supported values for this variable are
  ``INCLUDE`` and ``EXCLUDE``.

``PACKAGE_FIND_VERSION_MIN``
  Full requested version string of the lower end point of the range
``PACKAGE_FIND_VERSION_MIN_MAJOR``
  Major version of the lower end point if requested, else 0
``PACKAGE_FIND_VERSION_MIN_MINOR``
  Minor version of the lower end point if requested, else 0
``PACKAGE_FIND_VERSION_MIN_PATCH``
  Patch version of the lower end point if requested, else 0
``PACKAGE_FIND_VERSION_MIN_TWEAK``
  Tweak version of the lower end point if requested, else 0
``PACKAGE_FIND_VERSION_MIN_COUNT``
  Number of version components of the lower end point, 0 to 4

``PACKAGE_FIND_VERSION_MAX``
  Full requested version string of the upper end point of the range
``PACKAGE_FIND_VERSION_MAX_MAJOR``
  Major version of the upper end point if requested, else 0
``PACKAGE_FIND_VERSION_MAX_MINOR``
  Minor version of the upper end point if requested, else 0
``PACKAGE_FIND_VERSION_MAX_PATCH``
  Patch version of the upper end point if requested, else 0
``PACKAGE_FIND_VERSION_MAX_TWEAK``
  Tweak version of the upper end point if requested, else 0
``PACKAGE_FIND_VERSION_MAX_COUNT``
  Number of version components of the upper end point, 0 to 4

Regardless of whether a single version or a version range is specified, the
variable ``PACKAGE_FIND_VERSION_COMPLETE`` will be defined and will hold
the full requested version string as specified.

The version file checks whether it satisfies the requested version and
sets these variables:

``PACKAGE_VERSION``
  Full provided version string
``PACKAGE_VERSION_EXACT``
  True if version is exact match
``PACKAGE_VERSION_COMPATIBLE``
  True if version is compatible
``PACKAGE_VERSION_UNSUITABLE``
  True if unsuitable as any version

These variables are checked by the ``find_package`` command to determine
whether the configuration file provides an acceptable version.  They
are not available after the ``find_package`` call returns.  If the version
is acceptable the following variables are set:

``<PackageName>_VERSION``
  Full provided version string
``<PackageName>_VERSION_MAJOR``
  Major version if provided, else 0
``<PackageName>_VERSION_MINOR``
  Minor version if provided, else 0
``<PackageName>_VERSION_PATCH``
  Patch version if provided, else 0
``<PackageName>_VERSION_TWEAK``
  Tweak version if provided, else 0
``<PackageName>_VERSION_COUNT``
  Number of version components, 0 to 4

and the corresponding package configuration file is loaded.
When multiple package configuration files are available whose version files
claim compatibility with the version requested it is unspecified which
one is chosen: unless the variable :variable:`CMAKE_FIND_PACKAGE_SORT_ORDER`
is set no attempt is made to choose a highest or closest version number.

To control the order in which ``find_package`` checks for compatibility use
the two variables :variable:`CMAKE_FIND_PACKAGE_SORT_ORDER` and
:variable:`CMAKE_FIND_PACKAGE_SORT_DIRECTION`.
For instance in order to select the highest version one can set

.. code-block:: cmake

  SET(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
  SET(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)

before calling ``find_package``.

Search Procedure
^^^^^^^^^^^^^^^^

CMake constructs a set of possible installation prefixes for the
package.  Under each prefix several directories are searched for a
configuration file.  The tables below show the directories searched.
Each entry is meant for installation trees following Windows (``W``), UNIX
(``U``), or Apple (``A``) conventions::

  <prefix>/                                                       (W)
  <prefix>/(cmake|CMake)/                                         (W)
  <prefix>/<name>*/                                               (W)
  <prefix>/<name>*/(cmake|CMake)/                                 (W)
  <prefix>/(lib/<arch>|lib*|share)/cmake/<name>*/                 (U)
  <prefix>/(lib/<arch>|lib*|share)/<name>*/                       (U)
  <prefix>/(lib/<arch>|lib*|share)/<name>*/(cmake|CMake)/         (U)
  <prefix>/<name>*/(lib/<arch>|lib*|share)/cmake/<name>*/         (W/U)
  <prefix>/<name>*/(lib/<arch>|lib*|share)/<name>*/               (W/U)
  <prefix>/<name>*/(lib/<arch>|lib*|share)/<name>*/(cmake|CMake)/ (W/U)

On systems supporting macOS :prop_tgt:`FRAMEWORK` and :prop_tgt:`BUNDLE`, the
following directories are searched for Frameworks or Application Bundles
containing a configuration file::

  <prefix>/<name>.framework/Resources/                    (A)
  <prefix>/<name>.framework/Resources/CMake/              (A)
  <prefix>/<name>.framework/Versions/*/Resources/         (A)
  <prefix>/<name>.framework/Versions/*/Resources/CMake/   (A)
  <prefix>/<name>.app/Contents/Resources/                 (A)
  <prefix>/<name>.app/Contents/Resources/CMake/           (A)

In all cases the ``<name>`` is treated as case-insensitive and corresponds
to any of the names specified (``<PackageName>`` or names given by ``NAMES``).

Paths with ``lib/<arch>`` are enabled if the
:variable:`CMAKE_LIBRARY_ARCHITECTURE` variable is set. ``lib*`` includes one
or more of the values ``lib64``, ``lib32``, ``libx32`` or ``lib`` (searched in
that order).

* Paths with ``lib64`` are searched on 64 bit platforms if the
  :prop_gbl:`FIND_LIBRARY_USE_LIB64_PATHS` property is set to ``TRUE``.
* Paths with ``lib32`` are searched on 32 bit platforms if the
  :prop_gbl:`FIND_LIBRARY_USE_LIB32_PATHS` property is set to ``TRUE``.
* Paths with ``libx32`` are searched on platforms using the x32 ABI
  if the :prop_gbl:`FIND_LIBRARY_USE_LIBX32_PATHS` property is set to ``TRUE``.
* The ``lib`` path is always searched.

If ``PATH_SUFFIXES`` is specified, the suffixes are appended to each
(``W``) or (``U``) directory entry one-by-one.

This set of directories is intended to work in cooperation with
projects that provide configuration files in their installation trees.
Directories above marked with (``W``) are intended for installations on
Windows where the prefix may point at the top of an application's
installation directory.  Those marked with (``U``) are intended for
installations on UNIX platforms where the prefix is shared by multiple
packages.  This is merely a convention, so all (``W``) and (``U``) directories
are still searched on all platforms.  Directories marked with (``A``) are
intended for installations on Apple platforms.  The
:variable:`CMAKE_FIND_FRAMEWORK` and :variable:`CMAKE_FIND_APPBUNDLE`
variables determine the order of preference.

The set of installation prefixes is constructed using the following
steps.  If ``NO_DEFAULT_PATH`` is specified all ``NO_*`` options are
enabled.

1. Search paths specified in the :variable:`<PackageName>_ROOT` CMake
   variable and the :envvar:`<PackageName>_ROOT` environment variable,
   where ``<PackageName>`` is the package to be found.
   The package root variables are maintained as a stack so if
   called from within a find module, root paths from the parent's find
   module will also be searched after paths for the current package.
   This can be skipped if ``NO_PACKAGE_ROOT_PATH`` is passed or by setting
   the :variable:`CMAKE_FIND_USE_PACKAGE_ROOT_PATH` to ``FALSE``.
   See policy :policy:`CMP0074`.

2. Search paths specified in cmake-specific cache variables.  These
   are intended to be used on the command line with a ``-DVAR=value``.
   The values are interpreted as :ref:`semicolon-separated lists <CMake Language Lists>`.
   This can be skipped if ``NO_CMAKE_PATH`` is passed or by setting the
   :variable:`CMAKE_FIND_USE_CMAKE_PATH` to ``FALSE``:

   * :variable:`CMAKE_PREFIX_PATH`
   * :variable:`CMAKE_FRAMEWORK_PATH`
   * :variable:`CMAKE_APPBUNDLE_PATH`

3. Search paths specified in cmake-specific environment variables.
   These are intended to be set in the user's shell configuration,
   and therefore use the host's native path separator
   (``;`` on Windows and ``:`` on UNIX).
   This can be skipped if ``NO_CMAKE_ENVIRONMENT_PATH`` is passed or by setting
   the :variable:`CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH` to ``FALSE``:

   * ``<PackageName>_DIR``
   * :envvar:`CMAKE_PREFIX_PATH`
   * ``CMAKE_FRAMEWORK_PATH``
   * ``CMAKE_APPBUNDLE_PATH``

4. Search paths specified by the ``HINTS`` option.  These should be paths
   computed by system introspection, such as a hint provided by the
   location of another item already found.  Hard-coded guesses should
   be specified with the ``PATHS`` option.

5. Search the standard system environment variables.  This can be
   skipped if ``NO_SYSTEM_ENVIRONMENT_PATH`` is passed  or by setting the
   :variable:`CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH` to ``FALSE``. Path entries
   ending in ``/bin`` or ``/sbin`` are automatically converted to their
   parent directories:

   * ``PATH``

6. Search paths stored in the CMake :ref:`User Package Registry`.
   This can be skipped if ``NO_CMAKE_PACKAGE_REGISTRY`` is passed or by
   setting the variable :variable:`CMAKE_FIND_USE_PACKAGE_REGISTRY`
   to ``FALSE`` or the deprecated variable
   :variable:`CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY` to ``TRUE``.

   See the :manual:`cmake-packages(7)` manual for details on the user
   package registry.

7. Search cmake variables defined in the Platform files for the
   current system.  This can be skipped if ``NO_CMAKE_SYSTEM_PATH`` is
   passed or by setting the :variable:`CMAKE_FIND_USE_CMAKE_SYSTEM_PATH`
   to ``FALSE``:

   * :variable:`CMAKE_SYSTEM_PREFIX_PATH`
   * :variable:`CMAKE_SYSTEM_FRAMEWORK_PATH`
   * :variable:`CMAKE_SYSTEM_APPBUNDLE_PATH`

   The platform paths that these variables contain are locations that
   typically include installed software. An example being ``/usr/local`` for
   UNIX based platforms.

8. Search paths stored in the CMake :ref:`System Package Registry`.
   This can be skipped if ``NO_CMAKE_SYSTEM_PACKAGE_REGISTRY`` is passed
   or by setting the :variable:`CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY`
   variable to ``FALSE`` or the deprecated variable
   :variable:`CMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY` to ``TRUE``.

   See the :manual:`cmake-packages(7)` manual for details on the system
   package registry.

9. Search paths specified by the ``PATHS`` option.  These are typically
   hard-coded guesses.

.. |FIND_XXX| replace:: find_package
.. |FIND_ARGS_XXX| replace:: <PackageName>
.. |CMAKE_FIND_ROOT_PATH_MODE_XXX| replace::
   :variable:`CMAKE_FIND_ROOT_PATH_MODE_PACKAGE`

.. include:: FIND_XXX_ROOT.txt
.. include:: FIND_XXX_ORDER.txt

By default the value stored in the result variable will be the path at
which the file is found.  The :variable:`CMAKE_FIND_PACKAGE_RESOLVE_SYMLINKS`
variable may be set to ``TRUE`` before calling ``find_package`` in order
to resolve symbolic links and store the real path to the file.

Every non-REQUIRED ``find_package`` call can be disabled by setting the
:variable:`CMAKE_DISABLE_FIND_PACKAGE_<PackageName>` variable to ``TRUE``.

Package File Interface Variables
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When loading a find module or package configuration file ``find_package``
defines variables to provide information about the call arguments (and
restores their original state before returning):

``CMAKE_FIND_PACKAGE_NAME``
  The ``<PackageName>`` which is searched for
``<PackageName>_FIND_REQUIRED``
  True if ``REQUIRED`` option was given
``<PackageName>_FIND_QUIETLY``
  True if ``QUIET`` option was given
``<PackageName>_FIND_VERSION``
  Full requested version string
``<PackageName>_FIND_VERSION_MAJOR``
  Major version if requested, else 0
``<PackageName>_FIND_VERSION_MINOR``
  Minor version if requested, else 0
``<PackageName>_FIND_VERSION_PATCH``
  Patch version if requested, else 0
``<PackageName>_FIND_VERSION_TWEAK``
  Tweak version if requested, else 0
``<PackageName>_FIND_VERSION_COUNT``
  Number of version components, 0 to 4
``<PackageName>_FIND_VERSION_EXACT``
  True if ``EXACT`` option was given
``<PackageName>_FIND_COMPONENTS``
  List of requested components
``<PackageName>_FIND_REQUIRED_<c>``
  True if component ``<c>`` is required,
  false if component ``<c>`` is optional

When a version range is specified, the above version variables will hold
values based on the lower end of the version range.  This is to preserve
compatibility with packages that have not been implemented to expect version
ranges.  In addition, the version range will be described by the following
variables:

``<PackageName>_FIND_VERSION_RANGE``
  Full requested version range string
``<PackageName>_FIND_VERSION_RANGE_MIN``
  This specifies whether the lower end point of the version range is
  included or excluded.  Currently, ``INCLUDE`` is the only supported value.
``<PackageName>_FIND_VERSION_RANGE_MAX``
  This specifies whether the upper end point of the version range is
  included or excluded.  The possible values for this variable are
  ``INCLUDE`` or ``EXCLUDE``.

``<PackageName>_FIND_VERSION_MIN``
  Full requested version string of the lower end point of the range
``<PackageName>_FIND_VERSION_MIN_MAJOR``
  Major version of the lower end point if requested, else 0
``<PackageName>_FIND_VERSION_MIN_MINOR``
  Minor version of the lower end point if requested, else 0
``<PackageName>_FIND_VERSION_MIN_PATCH``
  Patch version of the lower end point if requested, else 0
``<PackageName>_FIND_VERSION_MIN_TWEAK``
  Tweak version of the lower end point if requested, else 0
``<PackageName>_FIND_VERSION_MIN_COUNT``
  Number of version components of the lower end point, 0 to 4

``<PackageName>_FIND_VERSION_MAX``
  Full requested version string of the upper end point of the range
``<PackageName>_FIND_VERSION_MAX_MAJOR``
  Major version of the upper end point if requested, else 0
``<PackageName>_FIND_VERSION_MAX_MINOR``
  Minor version of the upper end point if requested, else 0
``<PackageName>_FIND_VERSION_MAX_PATCH``
  Patch version of the upper end point if requested, else 0
``<PackageName>_FIND_VERSION_MAX_TWEAK``
  Tweak version of the upper end point if requested, else 0
``<PackageName>_FIND_VERSION_MAX_COUNT``
  Number of version components of the upper end point, 0 to 4

Regardless of whether a single version or a version range is specified, the
variable ``<PackageName>_FIND_VERSION_COMPLETE`` will be defined and will hold
the full requested version string as specified.

In Module mode the loaded find module is responsible to honor the
request detailed by these variables; see the find module for details.
In Config mode ``find_package`` handles ``REQUIRED``, ``QUIET``, and
``[version]`` options automatically but leaves it to the package
configuration file to handle components in a way that makes sense
for the package.  The package configuration file may set
``<PackageName>_FOUND`` to false to tell ``find_package`` that component
requirements are not satisfied.
