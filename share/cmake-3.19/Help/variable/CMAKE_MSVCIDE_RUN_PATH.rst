CMAKE_MSVCIDE_RUN_PATH
----------------------

.. versionadded:: 3.10

Extra PATH locations that should be used when executing
:command:`add_custom_command` or :command:`add_custom_target` when using the
:generator:`Visual Studio 9 2008` (or above) generator. This allows
for running commands and using dll's that the IDE environment is not aware of.

If not set explicitly the value is initialized by the ``CMAKE_MSVCIDE_RUN_PATH``
environment variable, if set, and otherwise left empty.
