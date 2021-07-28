file
----

File manipulation command.

Synopsis
^^^^^^^^

.. parsed-literal::

  `Reading`_
    file(`READ`_ <filename> <out-var> [...])
    file(`STRINGS`_ <filename> <out-var> [...])
    file(`\<HASH\> <HASH_>`_ <filename> <out-var>)
    file(`TIMESTAMP`_ <filename> <out-var> [...])
    file(`GET_RUNTIME_DEPENDENCIES`_ [...])

  `Writing`_
    file({`WRITE`_ | `APPEND`_} <filename> <content>...)
    file({`TOUCH`_ | `TOUCH_NOCREATE`_} [<file>...])
    file(`GENERATE`_ OUTPUT <output-file> [...])
    file(`CONFIGURE`_ OUTPUT <output-file> CONTENT <content> [...])

  `Filesystem`_
    file({`GLOB`_ | `GLOB_RECURSE`_} <out-var> [...] [<globbing-expr>...])
    file(`RENAME`_ <oldname> <newname>)
    file({`REMOVE`_ | `REMOVE_RECURSE`_ } [<files>...])
    file(`MAKE_DIRECTORY`_ [<dir>...])
    file({`COPY`_ | `INSTALL`_} <file>... DESTINATION <dir> [...])
    file(`SIZE`_ <filename> <out-var>)
    file(`READ_SYMLINK`_ <linkname> <out-var>)
    file(`CREATE_LINK`_ <original> <linkname> [...])
    file(`CHMOD`_ <files>... <directories>... PERMISSIONS <permissions>... [...])
    file(`CHMOD_RECURSE`_ <files>... <directories>... PERMISSIONS <permissions>... [...])

  `Path Conversion`_
    file(`REAL_PATH`_ <path> <out-var> [BASE_DIRECTORY <dir>])
    file(`RELATIVE_PATH`_ <out-var> <directory> <file>)
    file({`TO_CMAKE_PATH`_ | `TO_NATIVE_PATH`_} <path> <out-var>)

  `Transfer`_
    file(`DOWNLOAD`_ <url> [<file>] [...])
    file(`UPLOAD`_ <file> <url> [...])

  `Locking`_
    file(`LOCK`_ <path> [...])

  `Archiving`_
    file(`ARCHIVE_CREATE`_ OUTPUT <archive> PATHS <paths>... [...])
    file(`ARCHIVE_EXTRACT`_ INPUT <archive> [...])

Reading
^^^^^^^

.. _READ:

.. code-block:: cmake

  file(READ <filename> <variable>
       [OFFSET <offset>] [LIMIT <max-in>] [HEX])

Read content from a file called ``<filename>`` and store it in a
``<variable>``.  Optionally start from the given ``<offset>`` and
read at most ``<max-in>`` bytes.  The ``HEX`` option causes data to
be converted to a hexadecimal representation (useful for binary data). If the
``HEX`` option is specified, letters in the output (``a`` through ``f``) are in
lowercase.

.. _STRINGS:

.. code-block:: cmake

  file(STRINGS <filename> <variable> [<options>...])

Parse a list of ASCII strings from ``<filename>`` and store it in
``<variable>``.  Binary data in the file are ignored.  Carriage return
(``\r``, CR) characters are ignored.  The options are:

``LENGTH_MAXIMUM <max-len>``
 Consider only strings of at most a given length.

``LENGTH_MINIMUM <min-len>``
 Consider only strings of at least a given length.

``LIMIT_COUNT <max-num>``
 Limit the number of distinct strings to be extracted.

``LIMIT_INPUT <max-in>``
 Limit the number of input bytes to read from the file.

``LIMIT_OUTPUT <max-out>``
 Limit the number of total bytes to store in the ``<variable>``.

``NEWLINE_CONSUME``
 Treat newline characters (``\n``, LF) as part of string content
 instead of terminating at them.

``NO_HEX_CONVERSION``
 Intel Hex and Motorola S-record files are automatically converted to
 binary while reading unless this option is given.

``REGEX <regex>``
 Consider only strings that match the given regular expression.

``ENCODING <encoding-type>``
 Consider strings of a given encoding.  Currently supported encodings are:
 UTF-8, UTF-16LE, UTF-16BE, UTF-32LE, UTF-32BE.  If the ENCODING option
 is not provided and the file has a Byte Order Mark, the ENCODING option
 will be defaulted to respect the Byte Order Mark.

For example, the code

.. code-block:: cmake

  file(STRINGS myfile.txt myfile)

stores a list in the variable ``myfile`` in which each item is a line
from the input file.

.. _HASH:

.. code-block:: cmake

  file(<HASH> <filename> <variable>)

Compute a cryptographic hash of the content of ``<filename>`` and
store it in a ``<variable>``.  The supported ``<HASH>`` algorithm names
are those listed by the :ref:`string(\<HASH\>) <Supported Hash Algorithms>`
command.

.. _TIMESTAMP:

.. code-block:: cmake

  file(TIMESTAMP <filename> <variable> [<format>] [UTC])

Compute a string representation of the modification time of ``<filename>``
and store it in ``<variable>``.  Should the command be unable to obtain a
timestamp variable will be set to the empty string ("").

See the :command:`string(TIMESTAMP)` command for documentation of
the ``<format>`` and ``UTC`` options.

.. _GET_RUNTIME_DEPENDENCIES:

.. code-block:: cmake

  file(GET_RUNTIME_DEPENDENCIES
    [RESOLVED_DEPENDENCIES_VAR <deps_var>]
    [UNRESOLVED_DEPENDENCIES_VAR <unresolved_deps_var>]
    [CONFLICTING_DEPENDENCIES_PREFIX <conflicting_deps_prefix>]
    [EXECUTABLES [<executable_files>...]]
    [LIBRARIES [<library_files>...]]
    [MODULES [<module_files>...]]
    [DIRECTORIES [<directories>...]]
    [BUNDLE_EXECUTABLE <bundle_executable_file>]
    [PRE_INCLUDE_REGEXES [<regexes>...]]
    [PRE_EXCLUDE_REGEXES [<regexes>...]]
    [POST_INCLUDE_REGEXES [<regexes>...]]
    [POST_EXCLUDE_REGEXES [<regexes>...]]
    )

Recursively get the list of libraries depended on by the given files.

Please note that this sub-command is not intended to be used in project mode.
Instead, use it in an :command:`install(CODE)` or :command:`install(SCRIPT)`
block. For example:

.. code-block:: cmake

  install(CODE [[
    file(GET_RUNTIME_DEPENDENCIES
      # ...
      )
    ]])

The arguments are as follows:

``RESOLVED_DEPENDENCIES_VAR <deps_var>``
  Name of the variable in which to store the list of resolved dependencies.

``UNRESOLVED_DEPENDENCIES_VAR <unresolved_deps_var>``
  Name of the variable in which to store the list of unresolved dependencies.
  If this variable is not specified, and there are any unresolved dependencies,
  an error is issued.

``CONFLICTING_DEPENDENCIES_PREFIX <conflicting_deps_prefix>``
  Variable prefix in which to store conflicting dependency information.
  Dependencies are conflicting if two files with the same name are found in
  two different directories. The list of filenames that conflict are stored in
  ``<conflicting_deps_prefix>_FILENAMES``. For each filename, the list of paths
  that were found for that filename are stored in
  ``<conflicting_deps_prefix>_<filename>``.

``EXECUTABLES <executable_files>``
  List of executable files to read for dependencies. These are executables that
  are typically created with :command:`add_executable`, but they do not have to
  be created by CMake. On Apple platforms, the paths to these files determine
  the value of ``@executable_path`` when recursively resolving the libraries.
  Specifying any kind of library (``STATIC``, ``MODULE``, or ``SHARED``) here
  will result in undefined behavior.

``LIBRARIES <library_files>``
  List of library files to read for dependencies. These are libraries that are
  typically created with :command:`add_library(SHARED)`, but they do not have
  to be created by CMake. Specifying ``STATIC`` libraries, ``MODULE``
  libraries, or executables here will result in undefined behavior.

``MODULES <module_files>``
  List of loadable module files to read for dependencies. These are modules
  that are typically created with :command:`add_library(MODULE)`, but they do
  not have to be created by CMake. They are typically used by calling
  ``dlopen()`` at runtime rather than linked at link time with ``ld -l``.
  Specifying ``STATIC`` libraries, ``SHARED`` libraries, or executables here
  will result in undefined behavior.

``DIRECTORIES <directories>``
  List of additional directories to search for dependencies. On Linux
  platforms, these directories are searched if the dependency is not found in
  any of the other usual paths. If it is found in such a directory, a warning
  is issued, because it means that the file is incomplete (it does not list all
  of the directories that contain its dependencies). On Windows platforms,
  these directories are searched if the dependency is not found in any of the
  other search paths, but no warning is issued, because searching other paths
  is a normal part of Windows dependency resolution. On Apple platforms, this
  argument has no effect.

``BUNDLE_EXECUTABLE <bundle_executable_file>``
  Executable to treat as the "bundle executable" when resolving libraries. On
  Apple platforms, this argument determines the value of ``@executable_path``
  when recursively resolving libraries for ``LIBRARIES`` and ``MODULES`` files.
  It has no effect on ``EXECUTABLES`` files. On other platforms, it has no
  effect. This is typically (but not always) one of the executables in the
  ``EXECUTABLES`` argument which designates the "main" executable of the
  package.

The following arguments specify filters for including or excluding libraries to
be resolved. See below for a full description of how they work.

``PRE_INCLUDE_REGEXES <regexes>``
  List of pre-include regexes through which to filter the names of
  not-yet-resolved dependencies.

``PRE_EXCLUDE_REGEXES <regexes>``
  List of pre-exclude regexes through which to filter the names of
  not-yet-resolved dependencies.

``POST_INCLUDE_REGEXES <regexes>``
  List of post-include regexes through which to filter the names of resolved
  dependencies.

``POST_EXCLUDE_REGEXES <regexes>``
  List of post-exclude regexes through which to filter the names of resolved
  dependencies.

These arguments can be used to exclude unwanted system libraries when
resolving the dependencies, or to include libraries from a specific
directory. The filtering works as follows:

1. If the not-yet-resolved dependency matches any of the
   ``PRE_INCLUDE_REGEXES``, steps 2 and 3 are skipped, and the dependency
   resolution proceeds to step 4.
2. If the not-yet-resolved dependency matches any of the
   ``PRE_EXCLUDE_REGEXES``, dependency resolution stops for that dependency.
3. Otherwise, dependency resolution proceeds.
4. ``file(GET_RUNTIME_DEPENDENCIES)`` searches for the dependency according to
   the linking rules of the platform (see below).
5. If the dependency is found, and its full path matches one of the
   ``POST_INCLUDE_REGEXES``, the full path is added to the resolved
   dependencies, and ``file(GET_RUNTIME_DEPENDENCIES)`` recursively resolves
   that library's own dependencies. Otherwise, resolution proceeds to step 6.
6. If the dependency is found, but its full path matches one of the
   ``POST_EXCLUDE_REGEXES``, it is not added to the resolved dependencies, and
   dependency resolution stops for that dependency.
7. If the dependency is found, and its full path does not match either
   ``POST_INCLUDE_REGEXES`` or ``POST_EXCLUDE_REGEXES``, the full path is added
   to the resolved dependencies, and ``file(GET_RUNTIME_DEPENDENCIES)``
   recursively resolves that library's own dependencies.

Different platforms have different rules for how dependencies are resolved.
These specifics are described here.

On Linux platforms, library resolution works as follows:

1. If the depending file does not have any ``RUNPATH`` entries, and the library
   exists in one of the depending file's ``RPATH`` entries, or its parents', in
   that order, the dependency is resolved to that file.
2. Otherwise, if the depending file has any ``RUNPATH`` entries, and the
   library exists in one of those entries, the dependency is resolved to that
   file.
3. Otherwise, if the library exists in one of the directories listed by
   ``ldconfig``, the dependency is resolved to that file.
4. Otherwise, if the library exists in one of the ``DIRECTORIES`` entries, the
   dependency is resolved to that file. In this case, a warning is issued,
   because finding a file in one of the ``DIRECTORIES`` means that the
   depending file is not complete (it does not list all the directories from
   which it pulls dependencies).
5. Otherwise, the dependency is unresolved.

On Windows platforms, library resolution works as follows:

1. The dependent DLL name is converted to lowercase. Windows DLL names are
   case-insensitive, and some linkers mangle the case of the DLL dependency
   names. However, this makes it more difficult for ``PRE_INCLUDE_REGEXES``,
   ``PRE_EXCLUDE_REGEXES``, ``POST_INCLUDE_REGEXES``, and
   ``POST_EXCLUDE_REGEXES`` to properly filter DLL names - every regex would
   have to check for both uppercase and lowercase letters. For example:

   .. code-block:: cmake

     file(GET_RUNTIME_DEPENDENCIES
       # ...
       PRE_INCLUDE_REGEXES "^[Mm][Yy][Ll][Ii][Bb][Rr][Aa][Rr][Yy]\\.[Dd][Ll][Ll]$"
       )

   Converting the DLL name to lowercase allows the regexes to only match
   lowercase names, thus simplifying the regex. For example:

   .. code-block:: cmake

     file(GET_RUNTIME_DEPENDENCIES
       # ...
       PRE_INCLUDE_REGEXES "^mylibrary\\.dll$"
       )

   This regex will match ``mylibrary.dll`` regardless of how it is cased,
   either on disk or in the depending file. (For example, it will match
   ``mylibrary.dll``, ``MyLibrary.dll``, and ``MYLIBRARY.DLL``.)

   Please note that the directory portion of any resolved DLLs retains its
   casing and is not converted to lowercase. Only the filename portion is
   converted.

2. (**Not yet implemented**) If the depending file is a Windows Store app, and
   the dependency is listed as a dependency in the application's package
   manifest, the dependency is resolved to that file.
3. Otherwise, if the library exists in the same directory as the depending
   file, the dependency is resolved to that file.
4. Otherwise, if the library exists in either the operating system's
   ``system32`` directory or the ``Windows`` directory, in that order, the
   dependency is resolved to that file.
5. Otherwise, if the library exists in one of the directories specified by
   ``DIRECTORIES``, in the order they are listed, the dependency is resolved to
   that file. In this case, a warning is not issued, because searching other
   directories is a normal part of Windows library resolution.
6. Otherwise, the dependency is unresolved.

On Apple platforms, library resolution works as follows:

1. If the dependency starts with ``@executable_path/``, and an ``EXECUTABLES``
   argument is in the process of being resolved, and replacing
   ``@executable_path/`` with the directory of the executable yields an
   existing file, the dependency is resolved to that file.
2. Otherwise, if the dependency starts with ``@executable_path/``, and there is
   a ``BUNDLE_EXECUTABLE`` argument, and replacing ``@executable_path/`` with
   the directory of the bundle executable yields an existing file, the
   dependency is resolved to that file.
3. Otherwise, if the dependency starts with ``@loader_path/``, and replacing
   ``@loader_path/`` with the directory of the depending file yields an
   existing file, the dependency is resolved to that file.
4. Otherwise, if the dependency starts with ``@rpath/``, and replacing
   ``@rpath/`` with one of the ``RPATH`` entries of the depending file yields
   an existing file, the dependency is resolved to that file. Note that
   ``RPATH`` entries that start with ``@executable_path/`` or ``@loader_path/``
   also have these items replaced with the appropriate path.
5. Otherwise, if the dependency is an absolute file that exists, the dependency
   is resolved to that file.
6. Otherwise, the dependency is unresolved.

This function accepts several variables that determine which tool is used for
dependency resolution:

.. variable:: CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM

  Determines which operating system and executable format the files are built
  for. This could be one of several values:

  * ``linux+elf``
  * ``windows+pe``
  * ``macos+macho``

  If this variable is not specified, it is determined automatically by system
  introspection.

.. variable:: CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL

  Determines the tool to use for dependency resolution. It could be one of
  several values, depending on the value of
  :variable:`CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM`:

  ================================================= =============================================
     ``CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM``       ``CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL``
  ================================================= =============================================
  ``linux+elf``                                     ``objdump``
  ``windows+pe``                                    ``dumpbin``
  ``windows+pe``                                    ``objdump``
  ``macos+macho``                                   ``otool``
  ================================================= =============================================

  If this variable is not specified, it is determined automatically by system
  introspection.

.. variable:: CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND

  Determines the path to the tool to use for dependency resolution. This is the
  actual path to ``objdump``, ``dumpbin``, or ``otool``.

  If this variable is not specified, it is determined by the value of
  ``CMAKE_OBJDUMP`` if set, else by system introspection.

Writing
^^^^^^^

.. _WRITE:
.. _APPEND:

.. code-block:: cmake

  file(WRITE <filename> <content>...)
  file(APPEND <filename> <content>...)

Write ``<content>`` into a file called ``<filename>``.  If the file does
not exist, it will be created.  If the file already exists, ``WRITE``
mode will overwrite it and ``APPEND`` mode will append to the end.
Any directories in the path specified by ``<filename>`` that do not
exist will be created.

If the file is a build input, use the :command:`configure_file` command
to update the file only when its content changes.

.. _TOUCH:
.. _TOUCH_NOCREATE:

.. code-block:: cmake

  file(TOUCH [<files>...])
  file(TOUCH_NOCREATE [<files>...])

Create a file with no content if it does not yet exist. If the file already
exists, its access and/or modification will be updated to the time when the
function call is executed.

Use TOUCH_NOCREATE to touch a file if it exists but not create it. If a file
does not exist it will be silently ignored.

With TOUCH and TOUCH_NOCREATE the contents of an existing file will not be
modified.

.. _GENERATE:

.. code-block:: cmake

  file(GENERATE OUTPUT output-file
       <INPUT input-file|CONTENT content>
       [CONDITION expression] [TARGET target])

Generate an output file for each build configuration supported by the current
:manual:`CMake Generator <cmake-generators(7)>`.  Evaluate
:manual:`generator expressions <cmake-generator-expressions(7)>`
from the input content to produce the output content.  The options are:

``CONDITION <condition>``
  Generate the output file for a particular configuration only if
  the condition is true.  The condition must be either ``0`` or ``1``
  after evaluating generator expressions.

``CONTENT <content>``
  Use the content given explicitly as input.

``INPUT <input-file>``
  Use the content from a given file as input.
  A relative path is treated with respect to the value of
  :variable:`CMAKE_CURRENT_SOURCE_DIR`.  See policy :policy:`CMP0070`.

``OUTPUT <output-file>``
  Specify the output file name to generate.  Use generator expressions
  such as ``$<CONFIG>`` to specify a configuration-specific output file
  name.  Multiple configurations may generate the same output file only
  if the generated content is identical.  Otherwise, the ``<output-file>``
  must evaluate to an unique name for each configuration.
  A relative path (after evaluating generator expressions) is treated
  with respect to the value of :variable:`CMAKE_CURRENT_BINARY_DIR`.
  See policy :policy:`CMP0070`.

``TARGET <target>``
  Specify which target to use when evaluating generator expressions that
  require a target for evaluation (e.g. ``$<COMPILE_FEATURES:...>``,
  ``$<TARGET_PROPERTY:prop>``).

Exactly one ``CONTENT`` or ``INPUT`` option must be given.  A specific
``OUTPUT`` file may be named by at most one invocation of ``file(GENERATE)``.
Generated files are modified and their timestamp updated on subsequent cmake
runs only if their content is changed.

Note also that ``file(GENERATE)`` does not create the output file until the
generation phase. The output file will not yet have been written when the
``file(GENERATE)`` command returns, it is written only after processing all
of a project's ``CMakeLists.txt`` files.

.. _CONFIGURE:

.. code-block:: cmake

  file(CONFIGURE OUTPUT output-file
       CONTENT content
       [ESCAPE_QUOTES] [@ONLY]
       [NEWLINE_STYLE [UNIX|DOS|WIN32|LF|CRLF] ])

Generate an output file using the input given by ``CONTENT`` and substitute
variable values referenced as ``@VAR@`` or ``${VAR}`` contained therein. The
substitution rules behave the same as the :command:`configure_file` command.
In order to match :command:`configure_file`'s behavior, generator expressions
are not supported for both ``OUTPUT`` and ``CONTENT``.

The arguments are:

``OUTPUT <output-file>``
  Specify the output file name to generate. A relative path is treated with
  respect to the value of :variable:`CMAKE_CURRENT_BINARY_DIR`.
  ``<output-file>`` does not support generator expressions.

``CONTENT <content>``
  Use the content given explicitly as input.
  ``<content>`` does not support generator expressions.

``ESCAPE_QUOTES``
  Escape any substituted quotes with backslashes (C-style).

``@ONLY``
  Restrict variable replacement to references of the form ``@VAR@``.
  This is useful for configuring scripts that use ``${VAR}`` syntax.

``NEWLINE_STYLE <style>``
  Specify the newline style for the output file.  Specify
  ``UNIX`` or ``LF`` for ``\n`` newlines, or specify
  ``DOS``, ``WIN32``, or ``CRLF`` for ``\r\n`` newlines.

Filesystem
^^^^^^^^^^

.. _GLOB:
.. _GLOB_RECURSE:

.. code-block:: cmake

  file(GLOB <variable>
       [LIST_DIRECTORIES true|false] [RELATIVE <path>] [CONFIGURE_DEPENDS]
       [<globbing-expressions>...])
  file(GLOB_RECURSE <variable> [FOLLOW_SYMLINKS]
       [LIST_DIRECTORIES true|false] [RELATIVE <path>] [CONFIGURE_DEPENDS]
       [<globbing-expressions>...])

Generate a list of files that match the ``<globbing-expressions>`` and
store it into the ``<variable>``.  Globbing expressions are similar to
regular expressions, but much simpler.  If ``RELATIVE`` flag is
specified, the results will be returned as relative paths to the given
path.  The results will be ordered lexicographically.

On Windows and macOS, globbing is case-insensitive even if the underlying
filesystem is case-sensitive (both filenames and globbing expressions are
converted to lowercase before matching).  On other platforms, globbing is
case-sensitive.

If the ``CONFIGURE_DEPENDS`` flag is specified, CMake will add logic
to the main build system check target to rerun the flagged ``GLOB`` commands
at build time. If any of the outputs change, CMake will regenerate the build
system.

By default ``GLOB`` lists directories - directories are omitted in result if
``LIST_DIRECTORIES`` is set to false.

.. note::
  We do not recommend using GLOB to collect a list of source files from
  your source tree.  If no CMakeLists.txt file changes when a source is
  added or removed then the generated build system cannot know when to
  ask CMake to regenerate.
  The ``CONFIGURE_DEPENDS`` flag may not work reliably on all generators, or if
  a new generator is added in the future that cannot support it, projects using
  it will be stuck. Even if ``CONFIGURE_DEPENDS`` works reliably, there is
  still a cost to perform the check on every rebuild.

Examples of globbing expressions include::

  *.cxx      - match all files with extension cxx
  *.vt?      - match all files with extension vta,...,vtz
  f[3-5].txt - match files f3.txt, f4.txt, f5.txt

The ``GLOB_RECURSE`` mode will traverse all the subdirectories of the
matched directory and match the files.  Subdirectories that are symlinks
are only traversed if ``FOLLOW_SYMLINKS`` is given or policy
:policy:`CMP0009` is not set to ``NEW``.

By default ``GLOB_RECURSE`` omits directories from result list - setting
``LIST_DIRECTORIES`` to true adds directories to result list.
If ``FOLLOW_SYMLINKS`` is given or policy :policy:`CMP0009` is not set to
``NEW`` then ``LIST_DIRECTORIES`` treats symlinks as directories.

Examples of recursive globbing include::

  /dir/*.py  - match all python files in /dir and subdirectories

.. _RENAME:

.. code-block:: cmake

  file(RENAME <oldname> <newname>)

Move a file or directory within a filesystem from ``<oldname>`` to
``<newname>``, replacing the destination atomically.

.. _REMOVE:
.. _REMOVE_RECURSE:

.. code-block:: cmake

  file(REMOVE [<files>...])
  file(REMOVE_RECURSE [<files>...])

Remove the given files.  The ``REMOVE_RECURSE`` mode will remove the given
files and directories, also non-empty directories. No error is emitted if a
given file does not exist.  Relative input paths are evaluated with respect
to the current source directory.  Empty input paths are ignored with a warning.

.. _MAKE_DIRECTORY:

.. code-block:: cmake

  file(MAKE_DIRECTORY [<directories>...])

Create the given directories and their parents as needed.

.. _COPY:
.. _INSTALL:

.. code-block:: cmake

  file(<COPY|INSTALL> <files>... DESTINATION <dir>
       [FILE_PERMISSIONS <permissions>...]
       [DIRECTORY_PERMISSIONS <permissions>...]
       [NO_SOURCE_PERMISSIONS] [USE_SOURCE_PERMISSIONS]
       [FOLLOW_SYMLINK_CHAIN]
       [FILES_MATCHING]
       [[PATTERN <pattern> | REGEX <regex>]
        [EXCLUDE] [PERMISSIONS <permissions>...]] [...])

The ``COPY`` signature copies files, directories, and symlinks to a
destination folder.  Relative input paths are evaluated with respect
to the current source directory, and a relative destination is
evaluated with respect to the current build directory.  Copying
preserves input file timestamps, and optimizes out a file if it exists
at the destination with the same timestamp.  Copying preserves input
permissions unless explicit permissions or ``NO_SOURCE_PERMISSIONS``
are given (default is ``USE_SOURCE_PERMISSIONS``).

If ``FOLLOW_SYMLINK_CHAIN`` is specified, ``COPY`` will recursively resolve
the symlinks at the paths given until a real file is found, and install
a corresponding symlink in the destination for each symlink encountered. For
each symlink that is installed, the resolution is stripped of the directory,
leaving only the filename, meaning that the new symlink points to a file in
the same directory as the symlink. This feature is useful on some Unix systems,
where libraries are installed as a chain of symlinks with version numbers, with
less specific versions pointing to more specific versions.
``FOLLOW_SYMLINK_CHAIN`` will install all of these symlinks and the library
itself into the destination directory. For example, if you have the following
directory structure:

* ``/opt/foo/lib/libfoo.so.1.2.3``
* ``/opt/foo/lib/libfoo.so.1.2 -> libfoo.so.1.2.3``
* ``/opt/foo/lib/libfoo.so.1 -> libfoo.so.1.2``
* ``/opt/foo/lib/libfoo.so -> libfoo.so.1``

and you do:

.. code-block:: cmake

  file(COPY /opt/foo/lib/libfoo.so DESTINATION lib FOLLOW_SYMLINK_CHAIN)

This will install all of the symlinks and ``libfoo.so.1.2.3`` itself into
``lib``.

See the :command:`install(DIRECTORY)` command for documentation of
permissions, ``FILES_MATCHING``, ``PATTERN``, ``REGEX``, and
``EXCLUDE`` options.  Copying directories preserves the structure
of their content even if options are used to select a subset of
files.

The ``INSTALL`` signature differs slightly from ``COPY``: it prints
status messages (subject to the :variable:`CMAKE_INSTALL_MESSAGE` variable),
and ``NO_SOURCE_PERMISSIONS`` is default.
Installation scripts generated by the :command:`install` command
use this signature (with some undocumented options for internal use).

.. _SIZE:

.. code-block:: cmake

  file(SIZE <filename> <variable>)

Determine the file size of the ``<filename>`` and put the result in
``<variable>`` variable. Requires that ``<filename>`` is a valid path
pointing to a file and is readable.

.. _READ_SYMLINK:

.. code-block:: cmake

  file(READ_SYMLINK <linkname> <variable>)

This subcommand queries the symlink ``<linkname>`` and stores the path it
points to in the result ``<variable>``.  If ``<linkname>`` does not exist or
is not a symlink, CMake issues a fatal error.

Note that this command returns the raw symlink path and does not resolve
a relative path.  The following is an example of how to ensure that an
absolute path is obtained:

.. code-block:: cmake

  set(linkname "/path/to/foo.sym")
  file(READ_SYMLINK "${linkname}" result)
  if(NOT IS_ABSOLUTE "${result}")
    get_filename_component(dir "${linkname}" DIRECTORY)
    set(result "${dir}/${result}")
  endif()

.. _CREATE_LINK:

.. code-block:: cmake

  file(CREATE_LINK <original> <linkname>
       [RESULT <result>] [COPY_ON_ERROR] [SYMBOLIC])

Create a link ``<linkname>`` that points to ``<original>``.
It will be a hard link by default, but providing the ``SYMBOLIC`` option
results in a symbolic link instead.  Hard links require that ``original``
exists and is a file, not a directory.  If ``<linkname>`` already exists,
it will be overwritten.

The ``<result>`` variable, if specified, receives the status of the operation.
It is set to ``0`` upon success or an error message otherwise.  If ``RESULT``
is not specified and the operation fails, a fatal error is emitted.

Specifying ``COPY_ON_ERROR`` enables copying the file as a fallback if
creating the link fails.  It can be useful for handling situations such as
``<original>`` and ``<linkname>`` being on different drives or mount points,
which would make them unable to support a hard link.

.. _CHMOD:

.. code-block:: cmake

  file(CHMOD <files>... <directories>...
      [PERMISSIONS <permissions>...]
      [FILE_PERMISSIONS <permissions>...]
      [DIRECTORY_PERMISSIONS <permissions>...])

Set the permissions for the ``<files>...`` and ``<directories>...`` specified.
Valid permissions are  ``OWNER_READ``, ``OWNER_WRITE``, ``OWNER_EXECUTE``,
``GROUP_READ``, ``GROUP_WRITE``, ``GROUP_EXECUTE``, ``WORLD_READ``,
``WORLD_WRITE``, ``WORLD_EXECUTE``.

Valid combination of keywords are:

``PERMISSIONS``
  All items are changed.

``FILE_PERMISSIONS``
  Only files are changed.

``DIRECTORY_PERMISSIONS``
  Only directories are changed.

``PERMISSIONS`` and ``FILE_PERMISSIONS``
  ``FILE_PERMISSIONS`` overrides ``PERMISSIONS`` for files.

``PERMISSIONS`` and ``DIRECTORY_PERMISSIONS``
  ``DIRECTORY_PERMISSIONS`` overrides ``PERMISSIONS`` for directories.

``FILE_PERMISSIONS`` and ``DIRECTORY_PERMISSIONS``
  Use ``FILE_PERMISSIONS`` for files and ``DIRECTORY_PERMISSIONS`` for
  directories.


.. _CHMOD_RECURSE:

.. code-block:: cmake

  file(CHMOD_RECURSE <files>... <directories>...
       [PERMISSIONS <permissions>...]
       [FILE_PERMISSIONS <permissions>...]
       [DIRECTORY_PERMISSIONS <permissions>...])

Same as `CHMOD`_, but change the permissions of files and directories present in
the ``<directories>...`` recursively.

Path Conversion
^^^^^^^^^^^^^^^

.. _REAL_PATH:

.. code-block:: cmake

  file(REAL_PATH <path> <out-var> [BASE_DIRECTORY <dir>])

Compute the absolute path to an existing file or directory with symlinks
resolved.

If the provided ``<path>`` is a relative path, it is evaluated relative to the
given base directory ``<dir>``. If no base directory is provided, the default
base directory will be :variable:`CMAKE_CURRENT_SOURCE_DIR`.

.. _RELATIVE_PATH:

.. code-block:: cmake

  file(RELATIVE_PATH <variable> <directory> <file>)

Compute the relative path from a ``<directory>`` to a ``<file>`` and
store it in the ``<variable>``.

.. _TO_CMAKE_PATH:
.. _TO_NATIVE_PATH:

.. code-block:: cmake

  file(TO_CMAKE_PATH "<path>" <variable>)
  file(TO_NATIVE_PATH "<path>" <variable>)

The ``TO_CMAKE_PATH`` mode converts a native ``<path>`` into a cmake-style
path with forward-slashes (``/``).  The input can be a single path or a
system search path like ``$ENV{PATH}``.  A search path will be converted
to a cmake-style list separated by ``;`` characters.

The ``TO_NATIVE_PATH`` mode converts a cmake-style ``<path>`` into a native
path with platform-specific slashes (``\`` on Windows and ``/`` elsewhere).

Always use double quotes around the ``<path>`` to be sure it is treated
as a single argument to this command.

Transfer
^^^^^^^^

.. _DOWNLOAD:
.. _UPLOAD:

.. code-block:: cmake

  file(DOWNLOAD <url> [<file>] [<options>...])
  file(UPLOAD   <file> <url> [<options>...])

The ``DOWNLOAD`` subcommand downloads the given ``<url>`` to a local ``<file>``.
If ``<file>`` is not specified for ``file(DOWNLOAD)``, the file is not saved.
This can be useful if you want to know if a file can be downloaded (for example,
to check that it exists) without actually saving it anywhere. The ``UPLOAD``
mode uploads a local ``<file>`` to a given ``<url>``.

Options to both ``DOWNLOAD`` and ``UPLOAD`` are:

``INACTIVITY_TIMEOUT <seconds>``
  Terminate the operation after a period of inactivity.

``LOG <variable>``
  Store a human-readable log of the operation in a variable.

``SHOW_PROGRESS``
  Print progress information as status messages until the operation is
  complete.

``STATUS <variable>``
  Store the resulting status of the operation in a variable.
  The status is a ``;`` separated list of length 2.
  The first element is the numeric return value for the operation,
  and the second element is a string value for the error.
  A ``0`` numeric error means no error in the operation.

``TIMEOUT <seconds>``
  Terminate the operation after a given total time has elapsed.

``USERPWD <username>:<password>``
  Set username and password for operation.

``HTTPHEADER <HTTP-header>``
  HTTP header for operation. Suboption can be repeated several times.

``NETRC <level>``
  Specify whether the .netrc file is to be used for operation.  If this
  option is not specified, the value of the ``CMAKE_NETRC`` variable
  will be used instead.
  Valid levels are:

  ``IGNORED``
    The .netrc file is ignored.
    This is the default.
  ``OPTIONAL``
    The .netrc file is optional, and information in the URL is preferred.
    The file will be scanned to find which ever information is not specified
    in the URL.
  ``REQUIRED``
    The .netrc file is required, and information in the URL is ignored.

``NETRC_FILE <file>``
  Specify an alternative .netrc file to the one in your home directory,
  if the ``NETRC`` level is ``OPTIONAL`` or ``REQUIRED``. If this option
  is not specified, the value of the ``CMAKE_NETRC_FILE`` variable will
  be used instead.

If neither ``NETRC`` option is given CMake will check variables
``CMAKE_NETRC`` and ``CMAKE_NETRC_FILE``, respectively.

``TLS_VERIFY <ON|OFF>``
  Specify whether to verify the server certificate for ``https://`` URLs.
  The default is to *not* verify.

``TLS_CAINFO <file>``
  Specify a custom Certificate Authority file for ``https://`` URLs.

For ``https://`` URLs CMake must be built with OpenSSL support.  ``TLS/SSL``
certificates are not checked by default.  Set ``TLS_VERIFY`` to ``ON`` to
check certificates. If neither ``TLS`` option is given CMake will check
variables ``CMAKE_TLS_VERIFY`` and ``CMAKE_TLS_CAINFO``, respectively.

Additional options to ``DOWNLOAD`` are:

``EXPECTED_HASH ALGO=<value>``

  Verify that the downloaded content hash matches the expected value, where
  ``ALGO`` is one of the algorithms supported by ``file(<HASH>)``.
  If it does not match, the operation fails with an error. It is an error to
  specify this if ``DOWNLOAD`` is not given a ``<file>``.

``EXPECTED_MD5 <value>``
  Historical short-hand for ``EXPECTED_HASH MD5=<value>``. It is an error to
  specify this if ``DOWNLOAD`` is not given a ``<file>``.

Locking
^^^^^^^

.. _LOCK:

.. code-block:: cmake

  file(LOCK <path> [DIRECTORY] [RELEASE]
       [GUARD <FUNCTION|FILE|PROCESS>]
       [RESULT_VARIABLE <variable>]
       [TIMEOUT <seconds>])

Lock a file specified by ``<path>`` if no ``DIRECTORY`` option present and file
``<path>/cmake.lock`` otherwise. File will be locked for scope defined by
``GUARD`` option (default value is ``PROCESS``). ``RELEASE`` option can be used
to unlock file explicitly. If option ``TIMEOUT`` is not specified CMake will
wait until lock succeed or until fatal error occurs. If ``TIMEOUT`` is set to
``0`` lock will be tried once and result will be reported immediately. If
``TIMEOUT`` is not ``0`` CMake will try to lock file for the period specified
by ``<seconds>`` value. Any errors will be interpreted as fatal if there is no
``RESULT_VARIABLE`` option. Otherwise result will be stored in ``<variable>``
and will be ``0`` on success or error message on failure.

Note that lock is advisory - there is no guarantee that other processes will
respect this lock, i.e. lock synchronize two or more CMake instances sharing
some modifiable resources. Similar logic applied to ``DIRECTORY`` option -
locking parent directory doesn't prevent other ``LOCK`` commands to lock any
child directory or file.

Trying to lock file twice is not allowed.  Any intermediate directories and
file itself will be created if they not exist.  ``GUARD`` and ``TIMEOUT``
options ignored on ``RELEASE`` operation.

Archiving
^^^^^^^^^

.. _ARCHIVE_CREATE:

.. code-block:: cmake

  file(ARCHIVE_CREATE OUTPUT <archive>
    PATHS <paths>...
    [FORMAT <format>]
    [COMPRESSION <compression> [COMPRESSION_LEVEL <compression-level>]]
    [MTIME <mtime>]
    [VERBOSE])

Creates the specified ``<archive>`` file with the files and directories
listed in ``<paths>``.  Note that ``<paths>`` must list actual files or
directories, wildcards are not supported.

Use the ``FORMAT`` option to specify the archive format.  Supported values
for ``<format>`` are ``7zip``, ``gnutar``, ``pax``, ``paxr``, ``raw`` and
``zip``.  If ``FORMAT`` is not given, the default format is ``paxr``.

Some archive formats allow the type of compression to be specified.
The ``7zip`` and ``zip`` archive formats already imply a specific type of
compression.  The other formats use no compression by default, but can be
directed to do so with the ``COMPRESSION`` option.  Valid values for
``<compression>`` are ``None``, ``BZip2``, ``GZip``, ``XZ``, and ``Zstd``.

The compression level can be specified with the ``COMPRESSION_LEVEL`` option.
The ``<compression-level>`` should be between 0-9, with the default being 0.
The ``COMPRESSION`` option must be present when ``COMPRESSION_LEVEL`` is given.

.. note::
  With ``FORMAT`` set to ``raw`` only one file will be compressed with the
  compression type specified by ``COMPRESSION``.

The ``VERBOSE`` option enables verbose output for the archive operation.

To specify the modification time recorded in tarball entries, use
the ``MTIME`` option.

.. _ARCHIVE_EXTRACT:

.. code-block:: cmake

  file(ARCHIVE_EXTRACT INPUT <archive>
    [DESTINATION <dir>]
    [PATTERNS <patterns>...]
    [LIST_ONLY]
    [VERBOSE])

Extracts or lists the content of the specified ``<archive>``.

The directory where the content of the archive will be extracted to can
be specified using the ``DESTINATION`` option.  If the directory does not
exist, it will be created.  If ``DESTINATION`` is not given, the current
binary directory will be used.

If required, you may select which files and directories to list or extract
from the archive using the specified ``<patterns>``.  Wildcards are supported.
If the ``PATTERNS`` option is not given, the entire archive will be listed or
extracted.

``LIST_ONLY`` will list the files in the archive rather than extract them.

With ``VERBOSE``, the command will produce verbose output.
