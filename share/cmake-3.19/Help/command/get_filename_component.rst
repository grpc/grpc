get_filename_component
----------------------

Get a specific component of a full filename.

.. code-block:: cmake

  get_filename_component(<var> <FileName> <mode> [CACHE])

Sets ``<var>`` to a component of ``<FileName>``, where ``<mode>`` is one of:

::

 DIRECTORY = Directory without file name
 NAME      = File name without directory
 EXT       = File name longest extension (.b.c from d/a.b.c)
 NAME_WE   = File name with neither the directory nor the longest extension
 LAST_EXT  = File name last extension (.c from d/a.b.c)
 NAME_WLE  = File name with neither the directory nor the last extension
 PATH      = Legacy alias for DIRECTORY (use for CMake <= 2.8.11)

Paths are returned with forward slashes and have no trailing slashes.
If the optional ``CACHE`` argument is specified, the result variable is
added to the cache.

.. code-block:: cmake

  get_filename_component(<var> <FileName> <mode> [BASE_DIR <dir>] [CACHE])

Sets ``<var>`` to the absolute path of ``<FileName>``, where ``<mode>`` is one
of:

::

 ABSOLUTE  = Full path to file
 REALPATH  = Full path to existing file with symlinks resolved

If the provided ``<FileName>`` is a relative path, it is evaluated relative
to the given base directory ``<dir>``.  If no base directory is
provided, the default base directory will be
:variable:`CMAKE_CURRENT_SOURCE_DIR`.

Paths are returned with forward slashes and have no trailing slashes.  If the
optional ``CACHE`` argument is specified, the result variable is added to the
cache.

.. code-block:: cmake

  get_filename_component(<var> <FileName> PROGRAM [PROGRAM_ARGS <arg_var>] [CACHE])

The program in ``<FileName>`` will be found in the system search path or
left as a full path.  If ``PROGRAM_ARGS`` is present with ``PROGRAM``, then
any command-line arguments present in the ``<FileName>`` string are split
from the program name and stored in ``<arg_var>``.  This is used to
separate a program name from its arguments in a command line string.

.. note::

  The ``REALPATH`` and ``PROGRAM`` subcommands had been superseded,
  respectively, by :ref:`file(REAL_PATH) <REAL_PATH>` and
  :command:`separate_arguments(PROGRAM)` commands.
