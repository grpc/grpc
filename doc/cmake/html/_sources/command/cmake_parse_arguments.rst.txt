cmake_parse_arguments
---------------------

.. versionadded:: 3.5

Parse function or macro arguments.

.. code-block:: cmake

  cmake_parse_arguments(<prefix> <options> <one_value_keywords>
                        <multi_value_keywords> <args>...)

  cmake_parse_arguments(PARSE_ARGV <N> <prefix> <options>
                        <one_value_keywords> <multi_value_keywords>)

This command is for use in macros or functions.
It processes the arguments given to that macro or function,
and defines a set of variables which hold the values of the
respective options.

The first signature reads processes arguments passed in the ``<args>...``.
This may be used in either a :command:`macro` or a :command:`function`.

The ``PARSE_ARGV`` signature is only for use in a :command:`function`
body.  In this case the arguments that are parsed come from the
``ARGV#`` variables of the calling function.  The parsing starts with
the ``<N>``-th argument, where ``<N>`` is an unsigned integer.  This allows for
the values to have special characters like ``;`` in them.

The ``<options>`` argument contains all options for the respective macro,
i.e.  keywords which can be used when calling the macro without any value
following, like e.g.  the ``OPTIONAL`` keyword of the :command:`install`
command.

The ``<one_value_keywords>`` argument contains all keywords for this macro
which are followed by one value, like e.g. ``DESTINATION`` keyword of the
:command:`install` command.

The ``<multi_value_keywords>`` argument contains all keywords for this
macro which can be followed by more than one value, like e.g. the
``TARGETS`` or ``FILES`` keywords of the :command:`install` command.

.. note::

   All keywords shall be unique. I.e. every keyword shall only be specified
   once in either ``<options>``, ``<one_value_keywords>`` or
   ``<multi_value_keywords>``. A warning will be emitted if uniqueness is
   violated.

When done, ``cmake_parse_arguments`` will consider for each of the
keywords listed in ``<options>``, ``<one_value_keywords>`` and
``<multi_value_keywords>`` a variable composed of the given ``<prefix>``
followed by ``"_"`` and the name of the respective keyword.  These
variables will then hold the respective value from the argument list
or be undefined if the associated option could not be found.
For the ``<options>`` keywords, these will always be defined,
to ``TRUE`` or ``FALSE``, whether the option is in the argument list or not.

All remaining arguments are collected in a variable
``<prefix>_UNPARSED_ARGUMENTS`` that will be undefined if all arguments
were recognized. This can be checked afterwards to see
whether your macro was called with unrecognized parameters.

``<one_value_keywords>`` and ``<multi_value_keywords>`` that were given no
values at all are collected in a variable ``<prefix>_KEYWORDS_MISSING_VALUES``
that will be undefined if all keywords received values. This can be checked
to see if there were keywords without any values given.

Consider the following example macro, ``my_install()``, which takes similar
arguments to the real :command:`install` command:

.. code-block:: cmake

   macro(my_install)
       set(options OPTIONAL FAST)
       set(oneValueArgs DESTINATION RENAME)
       set(multiValueArgs TARGETS CONFIGURATIONS)
       cmake_parse_arguments(MY_INSTALL "${options}" "${oneValueArgs}"
                             "${multiValueArgs}" ${ARGN} )

       # ...

Assume ``my_install()`` has been called like this:

.. code-block:: cmake

   my_install(TARGETS foo bar DESTINATION bin OPTIONAL blub CONFIGURATIONS)

After the ``cmake_parse_arguments`` call the macro will have set or undefined
the following variables::

   MY_INSTALL_OPTIONAL = TRUE
   MY_INSTALL_FAST = FALSE # was not used in call to my_install
   MY_INSTALL_DESTINATION = "bin"
   MY_INSTALL_RENAME <UNDEFINED> # was not used
   MY_INSTALL_TARGETS = "foo;bar"
   MY_INSTALL_CONFIGURATIONS <UNDEFINED> # was not used
   MY_INSTALL_UNPARSED_ARGUMENTS = "blub" # nothing expected after "OPTIONAL"
   MY_INSTALL_KEYWORDS_MISSING_VALUES = "CONFIGURATIONS"
            # No value for "CONFIGURATIONS" given

You can then continue and process these variables.

Keywords terminate lists of values, e.g. if directly after a
``one_value_keyword`` another recognized keyword follows, this is
interpreted as the beginning of the new option.  E.g.
``my_install(TARGETS foo DESTINATION OPTIONAL)`` would result in
``MY_INSTALL_DESTINATION`` set to ``"OPTIONAL"``, but as ``OPTIONAL``
is a keyword itself ``MY_INSTALL_DESTINATION`` will be empty (but added
to ``MY_INSTALL_KEYWORDS_MISSING_VALUES``) and ``MY_INSTALL_OPTIONAL`` will
therefore be set to ``TRUE``.
