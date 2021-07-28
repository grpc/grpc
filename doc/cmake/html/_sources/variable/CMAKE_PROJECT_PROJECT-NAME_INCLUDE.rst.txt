CMAKE_PROJECT_<PROJECT-NAME>_INCLUDE
------------------------------------

A CMake language file or module to be included as the last step of any
:command:`project` command calls that specify ``<PROJECT-NAME>`` as the project
name.  This is intended for injecting custom code into project builds without
modifying their source.

See also the :variable:`CMAKE_PROJECT_<PROJECT-NAME>_INCLUDE_BEFORE`,
:variable:`CMAKE_PROJECT_INCLUDE` and
:variable:`CMAKE_PROJECT_INCLUDE_BEFORE` variables.
