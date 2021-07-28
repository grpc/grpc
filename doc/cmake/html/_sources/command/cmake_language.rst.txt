cmake_language
--------------

.. versionadded:: 3.18

Call meta-operations on CMake commands.

Synopsis
^^^^^^^^

.. parsed-literal::

  cmake_language(`CALL`_ <command> [<arg>...])
  cmake_language(`EVAL`_ CODE <code>...)
  cmake_language(`DEFER`_ <options>... CALL <command> [<arg>...])

Introduction
^^^^^^^^^^^^

This command will call meta-operations on built-in CMake commands or
those created via the :command:`macro` or :command:`function` commands.

``cmake_language`` does not introduce a new variable or policy scope.

Calling Commands
^^^^^^^^^^^^^^^^

.. _CALL:

.. code-block:: cmake

  cmake_language(CALL <command> [<arg>...])

Calls the named ``<command>`` with the given arguments (if any).
For example, the code:

.. code-block:: cmake

  set(message_command "message")
  cmake_language(CALL ${message_command} STATUS "Hello World!")

is equivalent to

.. code-block:: cmake

  message(STATUS "Hello World!")

.. note::
  To ensure consistency of the code, the following commands are not allowed:

  * ``if`` / ``elseif`` / ``else`` / ``endif``
  * ``while`` / ``endwhile``
  * ``foreach`` / ``endforeach``
  * ``function`` / ``endfunction``
  * ``macro`` / ``endmacro``

Evaluating Code
^^^^^^^^^^^^^^^

.. _EVAL:

.. code-block:: cmake

  cmake_language(EVAL CODE <code>...)

Evaluates the ``<code>...`` as CMake code.

For example, the code:

.. code-block:: cmake

  set(A TRUE)
  set(B TRUE)
  set(C TRUE)
  set(condition "(A AND B) OR C")

  cmake_language(EVAL CODE "
    if (${condition})
      message(STATUS TRUE)
    else()
      message(STATUS FALSE)
    endif()"
  )

is equivalent to

.. code-block:: cmake

  set(A TRUE)
  set(B TRUE)
  set(C TRUE)
  set(condition "(A AND B) OR C")

  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/eval.cmake "
    if (${condition})
      message(STATUS TRUE)
    else()
      message(STATUS FALSE)
    endif()"
  )

  include(${CMAKE_CURRENT_BINARY_DIR}/eval.cmake)

Deferring Calls
^^^^^^^^^^^^^^^

.. versionadded:: 3.19

.. _DEFER:

.. code-block:: cmake

  cmake_language(DEFER <options>... CALL <command> [<arg>...])

Schedules a call to the named ``<command>`` with the given arguments (if any)
to occur at a later time.  By default, deferred calls are executed as if
written at the end of the current directory's ``CMakeLists.txt`` file,
except that they run even after a :command:`return` call.  Variable
references in arguments are evaluated at the time the deferred call is
executed.

The options are:

``DIRECTORY <dir>``
  Schedule the call for the end of the given directory instead of the
  current directory.  The ``<dir>`` may reference either a source
  directory or its corresponding binary directory.  Relative paths are
  treated as relative to the current source directory.

  The given directory must be known to CMake, being either the top-level
  directory or one added by :command:`add_subdirectory`.  Furthermore,
  the given directory must not yet be finished processing.  This means
  it can be the current directory or one of its ancestors.

``ID <id>``
  Specify an identification for the deferred call.
  The ``<id>`` may not be empty and may not begin with a capital letter ``A-Z``.
  The ``<id>`` may begin with an underscore (``_``) only if it was generated
  automatically by an earlier call that used ``ID_VAR`` to get the id.

``ID_VAR <var>``
  Specify a variable in which to store the identification for the
  deferred call.  If ``ID <id>`` is not given, a new identification
  will be generated and the generated id will start with an underscore (``_``).

The currently scheduled list of deferred calls may be retrieved:

.. code-block:: cmake

  cmake_language(DEFER [DIRECTORY <dir>] GET_CALL_IDS <var>)

This will store in ``<var>`` a :ref:`semicolon-separated list <CMake Language
Lists>` of deferred call ids.  The ids are for the directory scope in which
the calls have been deferred to (i.e. where they will be executed), which can
be different to the scope in which they were created.  The ``DIRECTORY``
option can be used to specify the scope for which to retrieve the call ids.
If that option is not given, the call ids for the current directory scope will
be returned.

Details of a specific call may be retrieved from its id:

.. code-block:: cmake

  cmake_language(DEFER [DIRECTORY <dir>] GET_CALL <id> <var>)

This will store in ``<var>`` a :ref:`semicolon-separated list <CMake Language
Lists>` in which the first element is the name of the command to be
called, and the remaining elements are its unevaluated arguments (any
contained ``;`` characters are included literally and cannot be distinguished
from multiple arguments).  If multiple calls are scheduled with the same id,
this retrieves the first one.  If no call is scheduled with the given id in
the specified ``DIRECTORY`` scope (or the current directory scope if no
``DIRECTORY`` option is given), this stores an empty string in the variable.

Deferred calls may be canceled by their id:

.. code-block:: cmake

  cmake_language(DEFER [DIRECTORY <dir>] CANCEL_CALL <id>...)

This cancels all deferred calls matching any of the given ids in the specified
``DIRECTORY`` scope (or the current directory scope if no ``DIRECTORY`` option
is given).  Unknown ids are silently ignored.

Deferred Call Examples
""""""""""""""""""""""

For example, the code:

.. code-block:: cmake

  cmake_language(DEFER CALL message "${deferred_message}")
  cmake_language(DEFER ID_VAR id CALL message "Cancelled Message")
  cmake_language(DEFER CANCEL_CALL ${id})
  message("Immediate Message")
  set(deferred_message "Deferred Message")

prints::

  Immediate Message
  Deferred Message

The ``Cancelled Message`` is never printed because its command is
cancelled.  The ``deferred_message`` variable reference is not evaluated
until the call site, so it can be set after the deferred call is scheduled.

In order to evaluate variable references immediately when scheduling a
deferred call, wrap it using ``cmake_language(EVAL)``.  However, note that
arguments will be re-evaluated in the deferred call, though that can be
avoided by using bracket arguments.  For example:

.. code-block:: cmake

  set(deferred_message "Deferred Message 1")
  set(re_evaluated [[${deferred_message}]])
  cmake_language(EVAL CODE "
    cmake_language(DEFER CALL message [[${deferred_message}]])
    cmake_language(DEFER CALL message \"${re_evaluated}\")
  ")
  message("Immediate Message")
  set(deferred_message "Deferred Message 2")

also prints::

  Immediate Message
  Deferred Message 1
  Deferred Message 2
