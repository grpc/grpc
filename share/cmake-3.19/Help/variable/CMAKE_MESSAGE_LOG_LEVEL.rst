CMAKE_MESSAGE_LOG_LEVEL
-----------------------

.. versionadded:: 3.17

When set, this variable specifies the logging level used by the
:command:`message` command.  Valid values are the same as those for the
``--log-level`` command line option of the :manual:`cmake(1)` program.
If this variable is set and the ``--log-level`` command line option is
given, the command line option takes precedence.

The main advantage to using this variable is to make a log level persist
between CMake runs.  Setting it as a cache variable will ensure that
subsequent CMake runs will continue to use the chosen log level.

Projects should not set this variable, it is intended for users so that
they may control the log level according to their own needs.
