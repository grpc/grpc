# Non-Bazel native tests

This directory contains logic that wraps builds and tests from the non-bazel realm to make them runnable under bazel.

Examples: cmake builds, run_tests.py tests, artifacts, distribtests etc.

NOTE: all tests and their setup under this directory are currently EXPERIMENTAL.

## How it works

The `//tools/bazelify_tests:repo_archive` target produces an archive that contains grpc at the current head with all its submodules. The rule uses a few tricks to achieve this:

* Uses a workspace status command to obtain the commit SHAs of grpc and all submodules from the workspace.
* When running, it actually jailbreaks from the bazel execroot to access the bazel workspace and create the necessary archives.
* The produced archives are deterministic (they have the same checksum if neither grpc or its submodules have changed).
* The target is defined in such a way so that it behaves "reasonably" from bazel's perspective (always re-runs if commit SHAs have changed, can be cached if not).

After grpc source code is archived, the "bazelified" tests basically depend on the archive and they run a script under a docker container.
The script unpacks the archived grpc code and creates a temporary workspace (under bazel's target execroot) and then performs the actions that are needed
(e.g. run the run_tests.py test harness, run cmake build etc).

There are two ways the test targets can run under a docker container:

* When running on RBE, all actions run under a docker container by definition.
* When running locally, bazel will start a docker container for each action when [docker sandbox](https://bazel.build/remote/sandbox) is used.
  (Note that the docker sandbox currently [doesn't work on windows](https://github.com/bazelbuild/bazel/issues/19101))

In both cases, the docker image which is used for any given action is determined by the action's `exec_properties` and can be specified as a default
(e.g. by RBE toolchain or by setting `--experimental_docker_image` flag) or explicitly for each action. For most tests in this directory,
the test rules actually configure the `exec_properties` for you, based on selecting one of the gRPC's testing docker images.

## Run tests on RBE

```
# "--genrule_strategy=remote,local" allows fallback to local execution if action doesn't support running remotely
# (required to be able to run the //tools/bazelify_tests:repo_archive target).
tools/bazel --bazelrc=tools/remote_build/linux.bazelrc test --genrule_strategy=remote,local --workspace_status_command=tools/bazelify_tests/workspace_status_cmd.sh //tools/bazelify_tests/test:basic_tests_linux
```

## Run tests locally under bazel's docker sandbox

```
tools/bazel --bazelrc=tools/remote_build/linux_docker_sandbox.bazelrc test --workspace_status_command=tools/bazelify_tests/workspace_status_cmd.sh //tools/bazelify_tests/test:basic_tests_linux
```

