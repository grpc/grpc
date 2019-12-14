# Running Remote Builds with bazel

This allows you to spawn gRPC C/C++ remote build and tests from your workstation with
configuration that's very similar to what's used by our CI Kokoro.

Note that this will only work for gRPC team members (it requires access to the
remote build and execution cluster), others will need to rely on local test runs
and tests run by Kokoro CI.


## Prerequisites

- See [Installing Bazel](https://docs.bazel.build/versions/master/install.html) for instructions how to install bazel on your system.

- Setup application default credentials for running remote builds by following the ["Set credentials" section](https://cloud.google.com/remote-build-execution/docs/results-ui/getting-started-results-ui). (Note: for the ResultStore UI upload to work, you'll need a special kind of application default credentials, so if the build event upload doesn't work, doublecheck the instructions)


## Running remote build manually from dev workstation

Run from repository root (opt, dbg):
```
# manual run of bazel tests remotely on Foundry
bazel --bazelrc=tools/remote_build/manual.bazelrc test --config=opt //test/...
```

Sanitizer runs (asan, msan, tsan, ubsan):
```
# manual run of bazel tests remotely on Foundry with given sanitizer
bazel --bazelrc=tools/remote_build/manual.bazelrc test --config=asan //test/...
```

Run on Windows MSVC:
```
# manual run of bazel tests remotely on RBE Windows (must be run from Windows machine)
bazel --bazelrc=tools/remote_build/windows.bazelrc test --config=windows_opt //test/...
```

Run on MacOS (experimental for now):
```
# manual run of bazel tests on Mac (must be run from Mac machine)
# NOTE: it's not really a "remote execution", but uploads results to ResultStore
bazel --bazelrc=tools/remote_build/mac.bazelrc test --config=opt //test/...
```

Available command line options can be found in
[Bazel command line reference](https://docs.bazel.build/versions/master/command-line-reference.html)
