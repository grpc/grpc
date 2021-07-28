if
--

Conditionally execute a group of commands.

Synopsis
^^^^^^^^

.. code-block:: cmake

  if(<condition>)
    <commands>
  elseif(<condition>) # optional block, can be repeated
    <commands>
  else()              # optional block
    <commands>
  endif()

Evaluates the ``condition`` argument of the ``if`` clause according to the
`Condition syntax`_ described below. If the result is true, then the
``commands`` in the ``if`` block are executed.
Otherwise, optional ``elseif`` blocks are processed in the same way.
Finally, if no ``condition`` is true, ``commands`` in the optional ``else``
block are executed.

Per legacy, the :command:`else` and :command:`endif` commands admit
an optional ``<condition>`` argument.
If used, it must be a verbatim
repeat of the argument of the opening
``if`` command.

.. _`Condition Syntax`:

Condition Syntax
^^^^^^^^^^^^^^^^

The following syntax applies to the ``condition`` argument of
the ``if``, ``elseif`` and :command:`while` clauses.

Compound conditions are evaluated in the following order of precedence:
Innermost parentheses are evaluated first. Next come unary tests such
as ``EXISTS``, ``COMMAND``, and ``DEFINED``.  Then binary tests such as
``EQUAL``, ``LESS``, ``LESS_EQUAL``, ``GREATER``, ``GREATER_EQUAL``,
``STREQUAL``, ``STRLESS``, ``STRLESS_EQUAL``, ``STRGREATER``,
``STRGREATER_EQUAL``, ``VERSION_EQUAL``, ``VERSION_LESS``,
``VERSION_LESS_EQUAL``, ``VERSION_GREATER``, ``VERSION_GREATER_EQUAL``,
and ``MATCHES``.  Then the boolean operators in the order ``NOT``,  ``AND``,
and finally ``OR``.

Possible conditions are:

``if(<constant>)``
 True if the constant is ``1``, ``ON``, ``YES``, ``TRUE``, ``Y``,
 or a non-zero number.  False if the constant is ``0``, ``OFF``,
 ``NO``, ``FALSE``, ``N``, ``IGNORE``, ``NOTFOUND``, the empty string,
 or ends in the suffix ``-NOTFOUND``.  Named boolean constants are
 case-insensitive.  If the argument is not one of these specific
 constants, it is treated as a variable or string and the following
 signature is used.

``if(<variable|string>)``
 True if given a variable that is defined to a value that is not a false
 constant.  False otherwise.  (Note macro arguments are not variables.)

``if(NOT <condition>)``
 True if the condition is not true.

``if(<cond1> AND <cond2>)``
 True if both conditions would be considered true individually.

``if(<cond1> OR <cond2>)``
 True if either condition would be considered true individually.

``if(COMMAND command-name)``
 True if the given name is a command, macro or function that can be
 invoked.

``if(POLICY policy-id)``
 True if the given name is an existing policy (of the form ``CMP<NNNN>``).

``if(TARGET target-name)``
 True if the given name is an existing logical target name created
 by a call to the :command:`add_executable`, :command:`add_library`,
 or :command:`add_custom_target` command that has already been invoked
 (in any directory).

``if(TEST test-name)``
 True if the given name is an existing test name created by the
 :command:`add_test` command.

``if(EXISTS path-to-file-or-directory)``
 True if the named file or directory exists.  Behavior is well-defined
 only for full paths. Resolves symbolic links, i.e. if the named file or
 directory is a symbolic link, returns true if the target of the
 symbolic link exists.

``if(file1 IS_NEWER_THAN file2)``
 True if ``file1`` is newer than ``file2`` or if one of the two files doesn't
 exist.  Behavior is well-defined only for full paths.  If the file
 time stamps are exactly the same, an ``IS_NEWER_THAN`` comparison returns
 true, so that any dependent build operations will occur in the event
 of a tie.  This includes the case of passing the same file name for
 both file1 and file2.

``if(IS_DIRECTORY path-to-directory)``
 True if the given name is a directory.  Behavior is well-defined only
 for full paths.

``if(IS_SYMLINK file-name)``
 True if the given name is a symbolic link.  Behavior is well-defined
 only for full paths.

``if(IS_ABSOLUTE path)``
 True if the given path is an absolute path.

``if(<variable|string> MATCHES regex)``
 True if the given string or variable's value matches the given regular
 condition.  See :ref:`Regex Specification` for regex format.
 ``()`` groups are captured in :variable:`CMAKE_MATCH_<n>` variables.

``if(<variable|string> LESS <variable|string>)``
 True if the given string or variable's value is a valid number and less
 than that on the right.

``if(<variable|string> GREATER <variable|string>)``
 True if the given string or variable's value is a valid number and greater
 than that on the right.

``if(<variable|string> EQUAL <variable|string>)``
 True if the given string or variable's value is a valid number and equal
 to that on the right.

``if(<variable|string> LESS_EQUAL <variable|string>)``
 True if the given string or variable's value is a valid number and less
 than or equal to that on the right.

``if(<variable|string> GREATER_EQUAL <variable|string>)``
 True if the given string or variable's value is a valid number and greater
 than or equal to that on the right.

``if(<variable|string> STRLESS <variable|string>)``
 True if the given string or variable's value is lexicographically less
 than the string or variable on the right.

``if(<variable|string> STRGREATER <variable|string>)``
 True if the given string or variable's value is lexicographically greater
 than the string or variable on the right.

``if(<variable|string> STREQUAL <variable|string>)``
 True if the given string or variable's value is lexicographically equal
 to the string or variable on the right.

``if(<variable|string> STRLESS_EQUAL <variable|string>)``
 True if the given string or variable's value is lexicographically less
 than or equal to the string or variable on the right.

``if(<variable|string> STRGREATER_EQUAL <variable|string>)``
 True if the given string or variable's value is lexicographically greater
 than or equal to the string or variable on the right.

``if(<variable|string> VERSION_LESS <variable|string>)``
 Component-wise integer version number comparison (version format is
 ``major[.minor[.patch[.tweak]]]``, omitted components are treated as zero).
 Any non-integer version component or non-integer trailing part of a version
 component effectively truncates the string at that point.

``if(<variable|string> VERSION_GREATER <variable|string>)``
 Component-wise integer version number comparison (version format is
 ``major[.minor[.patch[.tweak]]]``, omitted components are treated as zero).
 Any non-integer version component or non-integer trailing part of a version
 component effectively truncates the string at that point.

``if(<variable|string> VERSION_EQUAL <variable|string>)``
 Component-wise integer version number comparison (version format is
 ``major[.minor[.patch[.tweak]]]``, omitted components are treated as zero).
 Any non-integer version component or non-integer trailing part of a version
 component effectively truncates the string at that point.

``if(<variable|string> VERSION_LESS_EQUAL <variable|string>)``
 Component-wise integer version number comparison (version format is
 ``major[.minor[.patch[.tweak]]]``, omitted components are treated as zero).
 Any non-integer version component or non-integer trailing part of a version
 component effectively truncates the string at that point.

``if(<variable|string> VERSION_GREATER_EQUAL <variable|string>)``
 Component-wise integer version number comparison (version format is
 ``major[.minor[.patch[.tweak]]]``, omitted components are treated as zero).
 Any non-integer version component or non-integer trailing part of a version
 component effectively truncates the string at that point.

``if(<variable|string> IN_LIST <variable>)``
 True if the given element is contained in the named list variable.

``if(DEFINED <name>|CACHE{<name>}|ENV{<name>})``
 True if a variable, cache variable or environment variable
 with given ``<name>`` is defined. The value of the variable
 does not matter. Note that macro arguments are not variables.

``if((condition) AND (condition OR (condition)))``
 The conditions inside the parenthesis are evaluated first and then
 the remaining condition is evaluated as in the previous examples.
 Where there are nested parenthesis the innermost are evaluated as part
 of evaluating the condition that contains them.

Variable Expansion
^^^^^^^^^^^^^^^^^^

The if command was written very early in CMake's history, predating
the ``${}`` variable evaluation syntax, and for convenience evaluates
variables named by its arguments as shown in the above signatures.
Note that normal variable evaluation with ``${}`` applies before the if
command even receives the arguments.  Therefore code like

.. code-block:: cmake

 set(var1 OFF)
 set(var2 "var1")
 if(${var2})

appears to the if command as

.. code-block:: cmake

  if(var1)

and is evaluated according to the ``if(<variable>)`` case documented
above.  The result is ``OFF`` which is false.  However, if we remove the
``${}`` from the example then the command sees

.. code-block:: cmake

  if(var2)

which is true because ``var2`` is defined to ``var1`` which is not a false
constant.

Automatic evaluation applies in the other cases whenever the
above-documented condition syntax accepts ``<variable|string>``:

* The left hand argument to ``MATCHES`` is first checked to see if it is
  a defined variable, if so the variable's value is used, otherwise the
  original value is used.

* If the left hand argument to ``MATCHES`` is missing it returns false
  without error

* Both left and right hand arguments to ``LESS``, ``GREATER``, ``EQUAL``,
  ``LESS_EQUAL``, and ``GREATER_EQUAL``, are independently tested to see if
  they are defined variables, if so their defined values are used otherwise
  the original value is used.

* Both left and right hand arguments to ``STRLESS``, ``STRGREATER``,
  ``STREQUAL``, ``STRLESS_EQUAL``, and ``STRGREATER_EQUAL`` are independently
  tested to see if they are defined variables, if so their defined values are
  used otherwise the original value is used.

* Both left and right hand arguments to ``VERSION_LESS``,
  ``VERSION_GREATER``, ``VERSION_EQUAL``, ``VERSION_LESS_EQUAL``, and
  ``VERSION_GREATER_EQUAL`` are independently tested to see if they are defined
  variables, if so their defined values are used otherwise the original value
  is used.

* The right hand argument to ``NOT`` is tested to see if it is a boolean
  constant, if so the value is used, otherwise it is assumed to be a
  variable and it is dereferenced.

* The left and right hand arguments to ``AND`` and ``OR`` are independently
  tested to see if they are boolean constants, if so they are used as
  such, otherwise they are assumed to be variables and are dereferenced.

To prevent ambiguity, potential variable or keyword names can be
specified in a :ref:`Quoted Argument` or a :ref:`Bracket Argument`.
A quoted or bracketed variable or keyword will be interpreted as a
string and not dereferenced or interpreted.
See policy :policy:`CMP0054`.

There is no automatic evaluation for environment or cache
:ref:`Variable References`.  Their values must be referenced as
``$ENV{<name>}`` or ``$CACHE{<name>}`` wherever the above-documented
condition syntax accepts ``<variable|string>``.
