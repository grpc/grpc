CMAKE_GENERATOR_PLATFORM
------------------------

.. versionadded:: 3.1

Generator-specific target platform specification provided by user.

Some CMake generators support a target platform name to be given
to the native build system to choose a compiler toolchain.
If the user specifies a platform name (e.g. via the :manual:`cmake(1)` ``-A``
option or via the :envvar:`CMAKE_GENERATOR_PLATFORM` environment variable)
the value will be available in this variable.

The value of this variable should never be modified by project code.
A toolchain file specified by the :variable:`CMAKE_TOOLCHAIN_FILE`
variable may initialize ``CMAKE_GENERATOR_PLATFORM``.  Once a given
build tree has been initialized with a particular value for this
variable, changing the value has undefined behavior.

Platform specification is supported only on specific generators:

* For :ref:`Visual Studio Generators` with VS 2005 and above this
  specifies the target architecture.

* For :generator:`Green Hills MULTI` this specifies the target architecture.

See native build system documentation for allowed platform names.

Visual Studio Platform Selection
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

On :ref:`Visual Studio Generators` the selected platform name
is provided in the :variable:`CMAKE_VS_PLATFORM_NAME` variable.
