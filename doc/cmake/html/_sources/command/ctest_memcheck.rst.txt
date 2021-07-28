ctest_memcheck
--------------

Perform the :ref:`CTest MemCheck Step` as a :ref:`Dashboard Client`.

::

  ctest_memcheck([BUILD <build-dir>] [APPEND]
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
                 [TEST_LOAD <threshold>]
                 [SCHEDULE_RANDOM <ON|OFF>]
                 [STOP_TIME <time-of-day>]
                 [RETURN_VALUE <result-var>]
                 [DEFECT_COUNT <defect-count-var>]
                 [QUIET]
                 )


Run tests with a dynamic analysis tool and store results in
``MemCheck.xml`` for submission with the :command:`ctest_submit`
command.

Most options are the same as those for the :command:`ctest_test` command.

The options unique to this command are:

``DEFECT_COUNT <defect-count-var>``
  Store in the ``<defect-count-var>`` the number of defects found.
