CMAKE_IGNORE_PATH
-----------------

:ref:`Semicolon-separated list <CMake Language Lists>` of directories to be *ignored* by
the :command:`find_program`, :command:`find_library`, :command:`find_file`,
and :command:`find_path` commands.  This is useful in cross-compiling
environments where some system directories contain incompatible but
possibly linkable libraries.  For example, on cross-compiled cluster
environments, this allows a user to ignore directories containing
libraries meant for the front-end machine.

By default this is empty; it is intended to be set by the project.
Note that ``CMAKE_IGNORE_PATH`` takes a list of directory names, *not*
a list of prefixes.  To ignore paths under prefixes (``bin``, ``include``,
``lib``, etc.), specify them explicitly.

See also the :variable:`CMAKE_PREFIX_PATH`, :variable:`CMAKE_LIBRARY_PATH`,
:variable:`CMAKE_INCLUDE_PATH`, and :variable:`CMAKE_PROGRAM_PATH` variables.
