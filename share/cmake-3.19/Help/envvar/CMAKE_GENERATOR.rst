CMAKE_GENERATOR
---------------

.. versionadded:: 3.15

.. include:: ENV_VAR.txt

Specifies the CMake default generator to use when no generator is supplied
with ``-G``. If the provided value doesn't name a generator known by CMake,
the internal default is used.  Either way the resulting generator selection
is stored in the :variable:`CMAKE_GENERATOR` variable.

Some generators may be additionally configured using the environment
variables:

* :envvar:`CMAKE_GENERATOR_PLATFORM`
* :envvar:`CMAKE_GENERATOR_TOOLSET`
* :envvar:`CMAKE_GENERATOR_INSTANCE`
