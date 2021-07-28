Ninja Multi-Config
------------------

.. versionadded:: 3.17

Generates multiple ``build-<Config>.ninja`` files.

This generator is very much like the :generator:`Ninja` generator, but with
some key differences. Only these differences will be discussed in this
document.

Unlike the :generator:`Ninja` generator, ``Ninja Multi-Config`` generates
multiple configurations at once with :variable:`CMAKE_CONFIGURATION_TYPES`
instead of only one configuration with :variable:`CMAKE_BUILD_TYPE`. One
``build-<Config>.ninja`` file will be generated for each of these
configurations (with ``<Config>`` being the configuration name.) These files
are intended to be run with ``ninja -f build-<Config>.ninja``. A
``build.ninja`` file is also generated, using the configuration from either
:variable:`CMAKE_DEFAULT_BUILD_TYPE` or the first item from
:variable:`CMAKE_CONFIGURATION_TYPES`.

``cmake --build . --config <Config>`` will always use ``build-<Config>.ninja``
to build. If no ``--config`` argument is specified, ``cmake --build .`` will
use ``build.ninja``.

Each ``build-<Config>.ninja`` file contains ``<target>`` targets as well as
``<target>:<Config>`` targets, where ``<Config>`` is the same as the
configuration specified in ``build-<Config>.ninja`` Additionally, if
cross-config mode is enabled, ``build-<Config>.ninja`` may contain
``<target>:<OtherConfig>`` targets, where ``<OtherConfig>`` is a cross-config,
as well as ``<target>:all``, which builds the target in all cross-configs. See
below for how to enable cross-config mode.

The ``Ninja Multi-Config`` generator recognizes the following variables:

:variable:`CMAKE_CONFIGURATION_TYPES`
  Specifies the total set of configurations to build.

:variable:`CMAKE_CROSS_CONFIGS`
  Specifies a :ref:`semicolon-separated list <CMake Language Lists>` of
  configurations available from all ``build-<Config>.ninja`` files.

:variable:`CMAKE_DEFAULT_BUILD_TYPE`
  Specifies the configuration to use by default in a ``build.ninja`` file.

:variable:`CMAKE_DEFAULT_CONFIGS`
  Specifies a :ref:`semicolon-separated list <CMake Language Lists>` of
  configurations to build for a target in ``build.ninja``
  if no ``:<Config>`` suffix is specified.

Consider the following example:

.. code-block:: cmake

  cmake_minimum_required(VERSION 3.16)
  project(MultiConfigNinja C)

  add_executable(generator generator.c)
  add_custom_command(OUTPUT generated.c COMMAND generator generated.c)
  add_library(generated ${CMAKE_BINARY_DIR}/generated.c)

Now assume you configure the project with ``Ninja Multi-Config`` and run one of
the following commands:

.. code-block:: shell

  ninja -f build-Debug.ninja generated
  # OR
  cmake --build . --config Debug --target generated

This would build the ``Debug`` configuration of ``generator``, which would be
used to generate ``generated.c``, which would be used to build the ``Debug``
configuration of ``generated``.

But if :variable:`CMAKE_CROSS_CONFIGS` is set to ``all``, and you run the
following instead:

.. code-block:: shell

  ninja -f build-Release.ninja generated:Debug
  # OR
  cmake --build . --config Release --target generated:Debug

This would build the ``Release`` configuration of ``generator``, which would be
used to generate ``generated.c``, which would be used to build the ``Debug``
configuration of ``generated``. This is useful for running a release-optimized
version of a generator utility while still building the debug version of the
targets built with the generated code.
