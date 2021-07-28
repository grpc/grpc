add_test
--------

Add a test to the project to be run by :manual:`ctest(1)`.

.. code-block:: cmake

  add_test(NAME <name> COMMAND <command> [<arg>...]
           [CONFIGURATIONS <config>...]
           [WORKING_DIRECTORY <dir>]
           [COMMAND_EXPAND_LISTS])

Adds a test called ``<name>``.  The test name may contain arbitrary
characters, expressed as a :ref:`Quoted Argument` or :ref:`Bracket Argument`
if necessary.  See policy :policy:`CMP0110`.  The options are:

``COMMAND``
  Specify the test command-line.  If ``<command>`` specifies an
  executable target (created by :command:`add_executable`) it will
  automatically be replaced by the location of the executable created
  at build time.

``CONFIGURATIONS``
  Restrict execution of the test only to the named configurations.

``WORKING_DIRECTORY``
  Set the :prop_test:`WORKING_DIRECTORY` test property to
  specify the working directory in which to execute the test.
  If not specified the test will be run with the current working
  directory set to the build directory corresponding to the
  current source directory.

``COMMAND_EXPAND_LISTS``
  Lists in ``COMMAND`` arguments will be expanded, including those
  created with
  :manual:`generator expressions <cmake-generator-expressions(7)>`.

The given test command is expected to exit with code ``0`` to pass and
non-zero to fail, or vice-versa if the :prop_test:`WILL_FAIL` test
property is set.  Any output written to stdout or stderr will be
captured by :manual:`ctest(1)` but does not affect the pass/fail status
unless the :prop_test:`PASS_REGULAR_EXPRESSION`,
:prop_test:`FAIL_REGULAR_EXPRESSION` or
:prop_test:`SKIP_REGULAR_EXPRESSION` test property is used.

The ``COMMAND`` and ``WORKING_DIRECTORY`` options may use "generator
expressions" with the syntax ``$<...>``.  See the
:manual:`cmake-generator-expressions(7)` manual for available expressions.

Example usage:

.. code-block:: cmake

  add_test(NAME mytest
           COMMAND testDriver --config $<CONFIGURATION>
                              --exe $<TARGET_FILE:myexe>)

This creates a test ``mytest`` whose command runs a ``testDriver`` tool
passing the configuration name and the full path to the executable
file produced by target ``myexe``.

.. note::

  CMake will generate tests only if the :command:`enable_testing`
  command has been invoked.  The :module:`CTest` module invokes the
  command automatically unless the ``BUILD_TESTING`` option is turned
  ``OFF``.

---------------------------------------------------------------------

.. code-block:: cmake

  add_test(<name> <command> [<arg>...])

Add a test called ``<name>`` with the given command-line.  Unlike
the above ``NAME`` signature no transformation is performed on the
command-line to support target names or generator expressions.
