CMAKE_VS_DEVENV_COMMAND
-----------------------

The generators for :generator:`Visual Studio 9 2008` and above set this
variable to the ``devenv.com`` command installed with the corresponding
Visual Studio version.  Note that this variable may be empty on
Visual Studio Express editions because they do not provide this tool.

This variable is not defined by other generators even if ``devenv.com``
is installed on the computer.

The :variable:`CMAKE_VS_MSBUILD_COMMAND` is also provided for
:generator:`Visual Studio 10 2010` and above.
See also the :variable:`CMAKE_MAKE_PROGRAM` variable.
