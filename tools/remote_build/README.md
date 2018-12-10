# Running Remote Builds with bazel

This allows you to spawn gRPC C/C++ remote build and tests from your workstation with
configuration that's very similar to what's used by our CI Kokoro.

Note that this will only work for gRPC team members (it requires access to the
remote build and execution cluster), others will need to rely on local test runs
and tests run by Kokoro CI.


## Prerequisites

- See [Installing Bazel](https://docs.bazel.build/versions/master/install.html) for instructions how to install bazel on your system.

- Setup application default credentials for running remote builds by following [RBE Credentials Setup](https://cloud.google.com/remote-build-execution/docs/getting-started#set_credentials)


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

Available command line options can be found in
[Bazel command line reference](https://docs.bazel.build/versions/master/command-line-reference.html)
