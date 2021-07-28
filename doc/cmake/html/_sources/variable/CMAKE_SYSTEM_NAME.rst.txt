CMAKE_SYSTEM_NAME
-----------------

The name of the operating system for which CMake is to build.
See the :variable:`CMAKE_SYSTEM_VERSION` variable for the OS version.

Note that ``CMAKE_SYSTEM_NAME`` is not set to anything by default when running
in script mode, since it's not building anything.

System Name for Host Builds
^^^^^^^^^^^^^^^^^^^^^^^^^^^

``CMAKE_SYSTEM_NAME`` is by default set to the same value as the
:variable:`CMAKE_HOST_SYSTEM_NAME` variable so that the build
targets the host system.

System Name for Cross Compiling
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``CMAKE_SYSTEM_NAME`` may be set explicitly when first configuring a new build
tree in order to enable :ref:`cross compiling <Cross Compiling Toolchain>`.
In this case the :variable:`CMAKE_SYSTEM_VERSION` variable must also be
set explicitly.
