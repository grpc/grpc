ctest_test
----------

Perform the :ref:`CTest Test Step` as a :ref:`Dashboard Client`.

::

  ctest_test([BUILD <build-dir>] [APPEND]
             [START <start-number>]
             [END <end-number>]
             [STRIDE <stride-number>]
             [EXCLUDE <exclude-regex>]
             [INCLUDE <include-regex>]
             [EXCLUDE_LABEL <label-exclude-regex>]
             [INCLUDE_LABEL <label-include-regex>]
             [EXCLUDE_FIXTURE <regex>]
             [EXCLUDE_FIXTURE_SETUP <regex>]
             [EXCLUDE_FIXTURE_CLEANUP <regex>]
             [PARALLEL_LEVEL <level>]
             [RESOURCE_SPEC_FILE <file>]
             [TEST_LOAD <threshold>]
             [SCHEDULE_RANDOM <ON|OFF>]
             [STOP_ON_FAILURE]
             [STOP_TIME <time-of-day>]
             [RETURN_VALUE <result-var>]
             [CAPTURE_CMAKE_ERROR <result-var>]
             [REPEAT <mode>:<n>]
             [QUIET]
             )

Run tests in the project build tree and store results in
``Test.xml`` for submission with the :command:`ctest_submit` command.

The options are:

``BUILD <build-dir>``
  Specify the top-level build directory.  If not given, the
  :variable:`CTEST_BINARY_DIRECTORY` variable is used.

``APPEND``
  Mark ``Test.xml`` for append to results previously submitted to a
  dashboard server since the last :command:`ctest_start` call.
  Append semantics are defined by the dashboard server in use.
  This does *not* cause results to be appended to a ``.xml`` file
  produced by a previous call to this command.

``START <start-number>``
  Specify the beginning of a range of test numbers.

``END <end-number>``
  Specify the end of a range of test numbers.

``STRIDE <stride-number>``
  Specify the stride by which to step across a range of test numbers.

``EXCLUDE <exclude-regex>``
  Specify a regular expression matching test names to exclude.

``INCLUDE <include-regex>``
  Specify a regular expression matching test names to include.
  Tests not matching this expression are excluded.

``EXCLUDE_LABEL <label-exclude-regex>``
  Specify a regular expression matching test labels to exclude.

``INCLUDE_LABEL <label-include-regex>``
  Specify a regular expression matching test labels to include.
  Tests not matching this expression are excluded.

``EXCLUDE_FIXTURE <regex>``
  If a test in the set of tests to be executed requires a particular fixture,
  that fixture's setup and cleanup tests would normally be added to the test
  set automatically. This option prevents adding setup or cleanup tests for
  fixtures matching the ``<regex>``. Note that all other fixture behavior is
  retained, including test dependencies and skipping tests that have fixture
  setup tests that fail.

``EXCLUDE_FIXTURE_SETUP <regex>``
  Same as ``EXCLUDE_FIXTURE`` except only matching setup tests are excluded.

``EXCLUDE_FIXTURE_CLEANUP <regex>``
  Same as ``EXCLUDE_FIXTURE`` except only matching cleanup tests are excluded.

``PARALLEL_LEVEL <level>``
  Specify a positive number representing the number of tests to
  be run in parallel.

``RESOURCE_SPEC_FILE <file>``
  Specify a
  :ref:`resource specification file <ctest-resource-specification-file>`. See
  :ref:`ctest-resource-allocation` for more information.

``TEST_LOAD <threshold>``
  While running tests in parallel, try not to start tests when they
  may cause the CPU load to pass above a given threshold.  If not
  specified the :variable:`CTEST_TEST_LOAD` variable will be checked,
  and then the ``--test-load`` command-line argument to :manual:`ctest(1)`.
  See also the ``TestLoad`` setting in the :ref:`CTest Test Step`.

``REPEAT <mode>:<n>``
  Run tests repeatedly based on the given ``<mode>`` up to ``<n>`` times.
  The modes are:

  ``UNTIL_FAIL``
    Require each test to run ``<n>`` times without failing in order to pass.
    This is useful in finding sporadic failures in test cases.

  ``UNTIL_PASS``
    Allow each test to run up to ``<n>`` times in order to pass.
    Repeats tests if they fail for any reason.
    This is useful in tolerating sporadic failures in test cases.

  ``AFTER_TIMEOUT``
    Allow each test to run up to ``<n>`` times in order to pass.
    Repeats tests only if they timeout.
    This is useful in tolerating sporadic timeouts in test cases
    on busy machines.

``SCHEDULE_RANDOM <ON|OFF>``
  Launch tests in a random order.  This may be useful for detecting
  implicit test dependencies.

``STOP_ON_FAILURE``
  Stop the execution of the tests once one has failed.

``STOP_TIME <time-of-day>``
  Specify a time of day at which the tests should all stop running.

``RETURN_VALUE <result-var>``
  Store in the ``<result-var>`` variable ``0`` if all tests passed.
  Store non-zero if anything went wrong.

``CAPTURE_CMAKE_ERROR <result-var>``
  Store in the ``<result-var>`` variable -1 if there are any errors running
  the command and prevent ctest from returning non-zero if an error occurs.

``QUIET``
  Suppress any CTest-specific non-error messages that would have otherwise
  been printed to the console.  Output from the underlying test command is not
  affected.  Summary info detailing the percentage of passing tests is also
  unaffected by the ``QUIET`` option.

See also the :variable:`CTEST_CUSTOM_MAXIMUM_PASSED_TEST_OUTPUT_SIZE`
and :variable:`CTEST_CUSTOM_MAXIMUM_FAILED_TEST_OUTPUT_SIZE` variables.
