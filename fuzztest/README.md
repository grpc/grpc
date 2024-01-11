Tests that leverage fuzztest.

These require C++17 and so cannot be run with the standard build configurations.

To run these tests:

bazel run //fuzztest/path/to:test -c dbg --config fuzztest
