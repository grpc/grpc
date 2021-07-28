CMAKE_PROJECT_INCLUDE
---------------------

.. versionadded:: 3.15

A CMake language file or module to be included as the last step of all
:command:`project` command calls.  This is intended for injecting custom code
into project builds without modifying their source.

See also the :variable:`CMAKE_PROJECT_<PROJECT-NAME>_INCLUDE`,
:variable:`CMAKE_PROJECT_<PROJECT-NAME>_INCLUDE_BEFORE` and
:variable:`CMAKE_PROJECT_INCLUDE_BEFORE` variables.
