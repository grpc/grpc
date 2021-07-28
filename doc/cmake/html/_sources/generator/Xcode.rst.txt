Xcode
-----

Generate Xcode project files.

This supports Xcode 5.0 and above.

.. _`Xcode Build System Selection`:

Toolset and Build System Selection
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

By default Xcode is allowed to select its own default toolchain.
The :variable:`CMAKE_GENERATOR_TOOLSET` option may be set, perhaps
via the :manual:`cmake(1)` ``-T`` option, to specify another toolset.

This generator supports toolset specification using one of these forms:

* ``toolset``
* ``toolset[,key=value]*``
* ``key=value[,key=value]*``

The ``toolset`` specifies the toolset name.  The selected toolset name
is provided in the :variable:`CMAKE_XCODE_PLATFORM_TOOLSET` variable.

The ``key=value`` pairs form a comma-separated list of options to
specify generator-specific details of the toolset selection.
Supported pairs are:

``buildsystem=<variant>``
  Specify the buildsystem variant to use.
  See the :variable:`CMAKE_XCODE_BUILD_SYSTEM` variable for allowed values.

  For example, to select the original build system under Xcode 12,
  run :manual:`cmake(1)` with the option ``-T buildsystem=1``.
