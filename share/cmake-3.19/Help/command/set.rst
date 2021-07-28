set
---

Set a normal, cache, or environment variable to a given value.
See the :ref:`cmake-language(7) variables <CMake Language Variables>`
documentation for the scopes and interaction of normal variables
and cache entries.

Signatures of this command that specify a ``<value>...`` placeholder
expect zero or more arguments.  Multiple arguments will be joined as
a :ref:`semicolon-separated list <CMake Language Lists>` to form the actual variable
value to be set.  Zero arguments will cause normal variables to be
unset.  See the :command:`unset` command to unset variables explicitly.

Set Normal Variable
^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  set(<variable> <value>... [PARENT_SCOPE])

Sets the given ``<variable>`` in the current function or directory scope.

If the ``PARENT_SCOPE`` option is given the variable will be set in
the scope above the current scope.  Each new directory or function
creates a new scope.  This command will set the value of a variable
into the parent directory or calling function (whichever is applicable
to the case at hand). The previous state of the variable's value stays the
same in the current scope (e.g., if it was undefined before, it is still
undefined and if it had a value, it is still that value).

Set Cache Entry
^^^^^^^^^^^^^^^

.. code-block:: cmake

  set(<variable> <value>... CACHE <type> <docstring> [FORCE])

Sets the given cache ``<variable>`` (cache entry).  Since cache entries
are meant to provide user-settable values this does not overwrite
existing cache entries by default.  Use the ``FORCE`` option to
overwrite existing entries.

The ``<type>`` must be specified as one of:

``BOOL``
  Boolean ``ON/OFF`` value.  :manual:`cmake-gui(1)` offers a checkbox.

``FILEPATH``
  Path to a file on disk.  :manual:`cmake-gui(1)` offers a file dialog.

``PATH``
  Path to a directory on disk.  :manual:`cmake-gui(1)` offers a file dialog.

``STRING``
  A line of text.  :manual:`cmake-gui(1)` offers a text field or a
  drop-down selection if the :prop_cache:`STRINGS` cache entry
  property is set.

``INTERNAL``
  A line of text.  :manual:`cmake-gui(1)` does not show internal entries.
  They may be used to store variables persistently across runs.
  Use of this type implies ``FORCE``.

The ``<docstring>`` must be specified as a line of text providing
a quick summary of the option for presentation to :manual:`cmake-gui(1)`
users.

If the cache entry does not exist prior to the call or the ``FORCE``
option is given then the cache entry will be set to the given value.
Furthermore, any normal variable binding in the current scope will
be removed to expose the newly cached value to any immediately
following evaluation.

It is possible for the cache entry to exist prior to the call but
have no type set if it was created on the :manual:`cmake(1)` command
line by a user through the ``-D<var>=<value>`` option without
specifying a type.  In this case the ``set`` command will add the
type.  Furthermore, if the ``<type>`` is ``PATH`` or ``FILEPATH``
and the ``<value>`` provided on the command line is a relative path,
then the ``set`` command will treat the path as relative to the
current working directory and convert it to an absolute path.

Set Environment Variable
^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: cmake

  set(ENV{<variable>} [<value>])

Sets an :manual:`Environment Variable <cmake-env-variables(7)>`
to the given value.
Subsequent calls of ``$ENV{<variable>}`` will return this new value.

This command affects only the current CMake process, not the process
from which CMake was called, nor the system environment at large,
nor the environment of subsequent build or test processes.

If no argument is given after ``ENV{<variable>}`` or if ``<value>`` is
an empty string, then this command will clear any existing value of the
environment variable.

Arguments after ``<value>`` are ignored. If extra arguments are found,
then an author warning is issued.
