CMAKE_VS_MSBUILD_COMMAND
------------------------

The generators for :generator:`Visual Studio 10 2010` and above set this
variable to the ``MSBuild.exe`` command installed with the corresponding
Visual Studio version.

This variable is not defined by other generators even if ``MSBuild.exe``
is installed on the computer.

The :variable:`CMAKE_VS_DEVENV_COMMAND` is also provided for the
non-Express editions of Visual Studio.
See also the :variable:`CMAKE_MAKE_PROGRAM` variable.
