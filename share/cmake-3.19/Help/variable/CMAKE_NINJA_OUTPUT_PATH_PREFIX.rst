CMAKE_NINJA_OUTPUT_PATH_PREFIX
------------------------------

.. versionadded:: 3.6

Set output files path prefix for the :generator:`Ninja` generator.

Every output files listed in the generated ``build.ninja`` will be
prefixed by the contents of this variable (a trailing slash is
appended if missing).  This is useful when the generated ninja file is
meant to be embedded as a ``subninja`` file into a *super* ninja
project.  For example, a ninja build file generated with a command
like::

  cd top-build-dir/sub &&
  cmake -G Ninja -DCMAKE_NINJA_OUTPUT_PATH_PREFIX=sub/ path/to/source

can be embedded in ``top-build-dir/build.ninja`` with a directive like
this::

  subninja sub/build.ninja

The ``auto-regeneration`` rule in ``top-build-dir/build.ninja`` must have an
order-only dependency on ``sub/build.ninja``.

.. note::
  When ``CMAKE_NINJA_OUTPUT_PATH_PREFIX`` is set, the project generated
  by CMake cannot be used as a standalone project.  No default targets
  are specified.
