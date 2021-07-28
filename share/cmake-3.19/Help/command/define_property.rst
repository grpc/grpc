define_property
---------------

Define and document custom properties.

.. code-block:: cmake

  define_property(<GLOBAL | DIRECTORY | TARGET | SOURCE |
                   TEST | VARIABLE | CACHED_VARIABLE>
                   PROPERTY <name> [INHERITED]
                   BRIEF_DOCS <brief-doc> [docs...]
                   FULL_DOCS <full-doc> [docs...])

Defines one property in a scope for use with the :command:`set_property` and
:command:`get_property` commands.  This is primarily useful to associate
documentation with property names that may be retrieved with the
:command:`get_property` command. The first argument determines the kind of
scope in which the property should be used.  It must be one of the
following:

::

  GLOBAL    = associated with the global namespace
  DIRECTORY = associated with one directory
  TARGET    = associated with one target
  SOURCE    = associated with one source file
  TEST      = associated with a test named with add_test
  VARIABLE  = documents a CMake language variable
  CACHED_VARIABLE = documents a CMake cache variable

Note that unlike :command:`set_property` and :command:`get_property` no
actual scope needs to be given; only the kind of scope is important.

The required ``PROPERTY`` option is immediately followed by the name of
the property being defined.

If the ``INHERITED`` option is given, then the :command:`get_property` command
will chain up to the next higher scope when the requested property is not set
in the scope given to the command.

* ``DIRECTORY`` scope chains to its parent directory's scope, continuing the
  walk up parent directories until a directory has the property set or there
  are no more parents.  If still not found at the top level directory, it
  chains to the ``GLOBAL`` scope.
* ``TARGET``, ``SOURCE`` and ``TEST`` properties chain to ``DIRECTORY`` scope,
  including further chaining up the directories, etc. as needed.

Note that this scope chaining behavior only applies to calls to
:command:`get_property`, :command:`get_directory_property`,
:command:`get_target_property`, :command:`get_source_file_property` and
:command:`get_test_property`.  There is no inheriting behavior when *setting*
properties, so using ``APPEND`` or ``APPEND_STRING`` with the
:command:`set_property` command will not consider inherited values when working
out the contents to append to.

The ``BRIEF_DOCS`` and ``FULL_DOCS`` options are followed by strings to be
associated with the property as its brief and full documentation.
Corresponding options to the :command:`get_property` command will retrieve
the documentation.
