CMAKE_VS_PLATFORM_TOOLSET_VERSION
---------------------------------

.. versionadded:: 3.12

Visual Studio Platform Toolset version.

The :ref:`Visual Studio Generators` for VS 2017 and above allow to
select minor versions of the same toolset. The toolset version number
may be specified by a field in :variable:`CMAKE_GENERATOR_TOOLSET` of
the form ``version=14.11``. If none is specified CMake will choose a default
toolset. The value may be empty if no minor version was selected and the
default is used.
