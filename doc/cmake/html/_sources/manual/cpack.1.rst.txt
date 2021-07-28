.. cmake-manual-description: CPack Command-Line Reference

cpack(1)
********

Synopsis
========

.. parsed-literal::

 cpack [<options>]

Description
===========

The **cpack** executable is the CMake packaging program.  It generates
installers and source packages in a variety of formats.

For each installer or package format, **cpack** has a specific backend,
called "generator". A generator is responsible for generating the required
inputs and invoking the specific package creation tools. These installer
or package generators are not to be confused with the makefile generators
of the :manual:`cmake <cmake(1)>` command.

All supported generators are specified in the :manual:`cpack-generators
<cpack-generators(7)>` manual.  The command ``cpack --help`` prints a
list of generators supported for the target platform.  Which of them are
to be used can be selected through the :variable:`CPACK_GENERATOR` variable
or through the command-line option ``-G``.

The **cpack** program is steered by a configuration file written in the
:manual:`CMake language <cmake-language(7)>`. Unless chosen differently
through the command-line option ``--config``, the file ``CPackConfig.cmake``
in the current directory is used.

In the standard CMake workflow, the file ``CPackConfig.cmake`` is generated
by the :manual:`cmake <cmake(1)>` executable, provided the :module:`CPack`
module is included by the project's ``CMakeLists.txt`` file.

Options
=======

``-G <generators>``
  ``<generators>`` is a :ref:`semicolon-separated list <CMake Language Lists>`
  of generator names.  ``cpack`` will iterate through this list and produce
  package(s) in that generator's format according to the details provided in
  the ``CPackConfig.cmake`` configuration file.  If this option is not given,
  the :variable:`CPACK_GENERATOR` variable determines the default set of
  generators that will be used.

``-C <configs>``
  Specify the project configuration(s) to be packaged (e.g. ``Debug``,
  ``Release``, etc.), where ``<configs>`` is a
  :ref:`semicolon-separated list <CMake Language Lists>`.
  When the CMake project uses a multi-configuration
  generator such as Xcode or Visual Studio, this option is needed to tell
  ``cpack`` which built executables to include in the package.
  The user is responsible for ensuring that the configuration(s) listed
  have already been built before invoking ``cpack``.

``-D <var>=<value>``
  Set a CPack variable.  This will override any value set for ``<var>`` in the
  input file read by ``cpack``.

``--config <configFile>``
  Specify the configuration file read by ``cpack`` to provide the packaging
  details.  By default, ``CPackConfig.cmake`` in the current directory will
  be used.

``--verbose, -V``
  Run ``cpack`` with verbose output.  This can be used to show more details
  from the package generation tools and is suitable for project developers.

``--debug``
  Run ``cpack`` with debug output.  This option is intended mainly for the
  developers of ``cpack`` itself and is not normally needed by project
  developers.

``--trace``
  Put the underlying cmake scripts in trace mode.

``--trace-expand``
  Put the underlying cmake scripts in expanded trace mode.

``-P <packageName>``
  Override/define the value of the :variable:`CPACK_PACKAGE_NAME` variable used
  for packaging.  Any value set for this variable in the ``CPackConfig.cmake``
  file will then be ignored.

``-R <packageVersion>``
  Override/define the value of the :variable:`CPACK_PACKAGE_VERSION`
  variable used for packaging.  It will override a value set in the
  ``CPackConfig.cmake`` file or one automatically computed from
  :variable:`CPACK_PACKAGE_VERSION_MAJOR`,
  :variable:`CPACK_PACKAGE_VERSION_MINOR` and
  :variable:`CPACK_PACKAGE_VERSION_PATCH`.

``-B <packageDirectory>``
  Override/define :variable:`CPACK_PACKAGE_DIRECTORY`, which controls the
  directory where CPack will perform its packaging work.  The resultant
  package(s) will be created at this location by default and a
  ``_CPack_Packages`` subdirectory will also be created below this directory to
  use as a working area during package creation.

``--vendor <vendorName>``
  Override/define :variable:`CPACK_PACKAGE_VENDOR`.

.. include:: OPTIONS_HELP.txt

See Also
========

.. include:: LINKS.txt
