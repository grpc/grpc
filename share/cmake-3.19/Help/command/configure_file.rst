configure_file
--------------

Copy a file to another location and modify its contents.

.. code-block:: cmake

  configure_file(<input> <output>
                 [COPYONLY] [ESCAPE_QUOTES] [@ONLY]
                 [NO_SOURCE_PERMISSIONS]
                 [NEWLINE_STYLE [UNIX|DOS|WIN32|LF|CRLF] ])

Copies an ``<input>`` file to an ``<output>`` file and substitutes
variable values referenced as ``@VAR@`` or ``${VAR}`` in the input
file content.  Each variable reference will be replaced with the
current value of the variable, or the empty string if the variable
is not defined.  Furthermore, input lines of the form

.. code-block:: c

  #cmakedefine VAR ...

will be replaced with either

.. code-block:: c

  #define VAR ...

or

.. code-block:: c

  /* #undef VAR */

depending on whether ``VAR`` is set in CMake to any value not considered
a false constant by the :command:`if` command.  The "..." content on the
line after the variable name, if any, is processed as above.
Input file lines of the form ``#cmakedefine01 VAR`` will be replaced with
either ``#define VAR 1`` or ``#define VAR 0`` similarly.
The result lines (with the exception of the ``#undef`` comments) can be
indented using spaces and/or tabs between the ``#`` character
and the ``cmakedefine`` or ``cmakedefine01`` words. This whitespace
indentation will be preserved in the output lines:

.. code-block:: c

  #  cmakedefine VAR
  #  cmakedefine01 VAR

will be replaced, if ``VAR`` is defined, with

.. code-block:: c

  #  define VAR
  #  define VAR 1

If the input file is modified the build system will re-run CMake to
re-configure the file and generate the build system again.
The generated file is modified and its timestamp updated on subsequent
cmake runs only if its content is changed.

The arguments are:

``<input>``
  Path to the input file.  A relative path is treated with respect to
  the value of :variable:`CMAKE_CURRENT_SOURCE_DIR`.  The input path
  must be a file, not a directory.

``<output>``
  Path to the output file or directory.  A relative path is treated
  with respect to the value of :variable:`CMAKE_CURRENT_BINARY_DIR`.
  If the path names an existing directory the output file is placed
  in that directory with the same file name as the input file.

``COPYONLY``
  Copy the file without replacing any variable references or other
  content.  This option may not be used with ``NEWLINE_STYLE``.

``ESCAPE_QUOTES``
  Escape any substituted quotes with backslashes (C-style).

``@ONLY``
  Restrict variable replacement to references of the form ``@VAR@``.
  This is useful for configuring scripts that use ``${VAR}`` syntax.

``NO_SOURCE_PERMISSIONS``
  Does not transfer the file permissions of the original file to the copy.
  The copied file permissions default to the standard 644 value
  (-rw-r--r--).

``NEWLINE_STYLE <style>``
  Specify the newline style for the output file.  Specify
  ``UNIX`` or ``LF`` for ``\n`` newlines, or specify
  ``DOS``, ``WIN32``, or ``CRLF`` for ``\r\n`` newlines.
  This option may not be used with ``COPYONLY``.

Example
^^^^^^^

Consider a source tree containing a ``foo.h.in`` file:

.. code-block:: c

  #cmakedefine FOO_ENABLE
  #cmakedefine FOO_STRING "@FOO_STRING@"

An adjacent ``CMakeLists.txt`` may use ``configure_file`` to
configure the header:

.. code-block:: cmake

  option(FOO_ENABLE "Enable Foo" ON)
  if(FOO_ENABLE)
    set(FOO_STRING "foo")
  endif()
  configure_file(foo.h.in foo.h @ONLY)

This creates a ``foo.h`` in the build directory corresponding to
this source directory.  If the ``FOO_ENABLE`` option is on, the
configured file will contain:

.. code-block:: c

  #define FOO_ENABLE
  #define FOO_STRING "foo"

Otherwise it will contain:

.. code-block:: c

  /* #undef FOO_ENABLE */
  /* #undef FOO_STRING */

One may then use the :command:`include_directories` command to
specify the output directory as an include directory:

.. code-block:: cmake

  include_directories(${CMAKE_CURRENT_BINARY_DIR})

so that sources may include the header as ``#include <foo.h>``.
