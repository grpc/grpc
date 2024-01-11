# Running Remote Builds with bazel

This allows you to spawn gRPC C/C++ remote build and tests from your workstation with
configuration that's very similar to what's used by our CI Kokoro.

Note that this will only work for gRPC team members (it requires access to the
remote build and execution cluster), others will need to rely on local test runs
and tests run by Kokoro CI.


## Prerequisites

- See [Installing Bazel](https://docs.bazel.build/versions/master/install.html) for instructions how to install bazel on your system.

- Setup Application Default Credentials (ADC) for running remote builds by following the ["Set credentials" section](https://developers.google.com/remote-build-execution/docs/set-up/first-remote-build#set_credentials). (Note: make sure that quota project "grpc-testing" was added to your ADC, you can double check the credential file at `${HOME}/.config/gcloud/application_default_credentials.json`)


## Running remote build manually from dev workstation

IMPORTANT: The OS from which you run the bazel command needs to always match your desired build & execution platform. If you want to run tests on linux, you need to run bazel from a linux machine, to execute tests on windows you need to be on windows etc. If you don't follow this guideline, the build might still appear like it's working, but you'll get nonsensical results (e.g. will be test configured as if on mac, but actually running on linux).

### Linux

For `opt` or `dbg` run this command:
```
# manual run of bazel tests remotely on Foundry
bazel --bazelrc=tools/remote_build/linux.bazelrc test --config=opt //test/...
```

This also works for sanitizer runs (`asan`, `msan`, `tsan`, `ubsan`):
```
# manual run of bazel tests remotely on Foundry with given sanitizer
bazel --bazelrc=tools/remote_build/linux.bazelrc test --config=asan //test/...
```

NOTE: If you see errors about Build Event Protocol upload failure (e.g. `ERROR: The Build Event Protocol upload failed: All retry attempts failed. PERMISSION_DENIED...`), try running `bazel shutdown` and try again.

### Windows

```
# manual run of bazel tests remotely on RBE Windows (must be run from Windows machine)
bazel --bazelrc=tools/remote_build/windows.bazelrc test --config=windows_opt //test/...
```

NOTE: Unlike on Linux and Mac, the bazel version won't get autoselected for you,
so check that you're using the [right bazel version](https://github.com/grpc/grpc/blob/master/tools/bazel).

### MacOS

There is no such thing as Mac RBE cluster, so a real remote build on Macs is currently impossible.
The following setup will build and run test on you local mac machine, but will give
you the RBE-like look & feel (e.g. a results link will be generated and some extra configuration will
be used).

```
# manual run of bazel tests on Mac (must be run from Mac machine)
# NOTE: it's not really a "remote execution", but uploads results to ResultStore
bazel --bazelrc=tools/remote_build/mac.bazelrc test --config=opt //test/...
```

NOTE: Because this is essentially a local run, you'll need to run start port server first (`tools/run_tests/start_port_server.py`)

## Running local builds with bazel

On all platforms, you can generally still use bazel builds & tests locally without any extra settings, but you might need to 
start port server first (`tools/run_tests/start_port_server.py`) to be able to run the tests locally.

E.g.: `bazel test --config=opt //test/...`

## Bazel command line options

Available command line options can be found in
[Bazel command line reference](https://docs.bazel.build/versions/master/command-line-reference.html)
