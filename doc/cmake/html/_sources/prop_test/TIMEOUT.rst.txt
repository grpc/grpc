TIMEOUT
-------

How many seconds to allow for this test.

This property if set will limit a test to not take more than the
specified number of seconds to run.  If it exceeds that the test
process will be killed and ctest will move to the next test.  This
setting takes precedence over :variable:`CTEST_TEST_TIMEOUT`.
