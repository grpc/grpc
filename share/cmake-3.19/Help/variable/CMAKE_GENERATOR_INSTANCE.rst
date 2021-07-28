CMAKE_GENERATOR_INSTANCE
------------------------

.. versionadded:: 3.11

Generator-specific instance specification provided by user.

Some CMake generators support selection of an instance of the native build
system when multiple instances are available.  If the user specifies an
instance (e.g. by setting this cache entry or via the
:envvar:`CMAKE_GENERATOR_INSTANCE` environment variable), or after a default
instance is chosen when a build tree is first configured, the value will be
available in this variable.

The value of this variable should never be modified by project code.
A toolchain file specified by the :variable:`CMAKE_TOOLCHAIN_FILE`
variable may initialize ``CMAKE_GENERATOR_INSTANCE`` as a cache entry.
Once a given build tree has been initialized with a particular value
for this variable, changing the value has undefined behavior.

Instance specification is supported only on specific generators:

* For the :generator:`Visual Studio 15 2017` generator (and above)
  this specifies the absolute path to the VS installation directory
  of the selected VS instance.

See native build system documentation for allowed instance values.
