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

*At the time being, tools/bazel.sh is used instead of invoking "bazel" directly
to overcome the bazel versioning problem (our BUILD files currently only work with
a specific window of bazel version and bazel.sh wrapper makes sure that version
is used).*

Run from repository root (opt, dbg):
```
# manual run of bazel tests remotely on Foundry
tools/bazel.sh --bazelrc=tools/remote_build/manual.bazelrc test --config=opt //test/...
```

Sanitizer runs (asan, msan, tsan, ubsan):
```
# manual run of bazel tests remotely on Foundry with given sanitizer
tools/bazel.sh --bazelrc=tools/remote_build/manual.bazelrc test --config=asan //test/...
```

Run on Windows MSVC:
```
# RBE manual run only for c-core (must be run on a Windows host machine)
tools/bazel.sh --bazelrc=tools/remote_build/windows.bazelrc build :all [--credentials_json=(path to service account credentials)]
```

Available command line options can be found in
[Bazel command line reference](https://docs.bazel.build/versions/master/command-line-reference.html)
