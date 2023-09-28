# Copyright 2023 The gRPC Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Contains macros used for running bazelified tests.
"""

load(":dockerimage_current_versions.bzl", "DOCKERIMAGE_CURRENT_VERSIONS")
load("@bazel_toolchains//rules/exec_properties:exec_properties.bzl", "create_rbe_exec_properties_dict")

def _dockerized_sh_test(name, srcs = [], args = [], data = [], size = "medium", timeout = None, tags = [], exec_compatible_with = [], flaky = None, docker_image_version = None, docker_run_as_root = False, env = {}):
    """Runs sh_test under docker either via RBE or via docker sandbox."""
    if docker_image_version:
        image_spec = DOCKERIMAGE_CURRENT_VERSIONS.get(docker_image_version, None)
        if not image_spec:
            fail("Version info for docker image '%s' not found in dockerimage_current_versions.bzl" % docker_image_version)
    else:
        fail("docker_image_version attribute not set for dockerized test '%s'" % name)

    exec_properties = create_rbe_exec_properties_dict(
        labels = {
            "workload": "misc",
            "machine_size": "misc_large",
        },
        docker_network = "standard",
        container_image = image_spec,
        # TODO(jtattermusch): note that docker sandbox doesn't currently support "docker_run_as_root"
        docker_run_as_root = docker_run_as_root,
    )

    # since the tests require special bazel args, only run them when explicitly requested
    tags = ["manual"] + tags

    # TODO(jtattermusch): find a way to ensure that action can only run under docker sandbox or remotely
    # to avoid running it outside of a docker container by accident.

    test_args = {
        "name": name,
        "srcs": srcs,
        "tags": tags,
        "args": args,
        "flaky": flaky,
        "data": data,
        "size": size,
        "env": env,
        "timeout": timeout,
        "exec_compatible_with": exec_compatible_with,
        "exec_properties": exec_properties,
    }

    native.sh_test(
        **test_args
    )

def _dockerized_genrule(name, cmd, outs, srcs = [], timeout = None, tags = [], exec_compatible_with = [], flaky = None, docker_image_version = None, docker_run_as_root = False):
    """Runs genrule under docker either via RBE or via docker sandbox."""
    if docker_image_version:
        image_spec = DOCKERIMAGE_CURRENT_VERSIONS.get(docker_image_version, None)
        if not image_spec:
            fail("Version info for docker image '%s' not found in dockerimage_current_versions.bzl" % docker_image_version)
    else:
        fail("docker_image_version attribute not set for dockerized test '%s'" % name)

    exec_properties = create_rbe_exec_properties_dict(
        labels = {
            "workload": "misc",
            "machine_size": "misc_large",
        },
        docker_network = "standard",
        container_image = image_spec,
        # TODO(jtattermusch): note that docker sandbox doesn't currently support "docker_run_as_root"
        docker_run_as_root = docker_run_as_root,
    )

    # since the tests require special bazel args, only run them when explicitly requested
    tags = ["manual"] + tags

    # TODO(jtattermusch): find a way to ensure that action can only run under docker sandbox or remotely
    # to avoid running it outside of a docker container by accident.

    genrule_args = {
        "name": name,
        "cmd": cmd,
        "srcs": srcs,
        "tags": tags,
        "flaky": flaky,
        "timeout": timeout,
        "exec_compatible_with": exec_compatible_with,
        "exec_properties": exec_properties,
        "outs": outs,
    }

    native.genrule(
        **genrule_args
    )

def grpc_run_tests_harness_test(name, args = [], data = [], size = "medium", timeout = None, tags = [], exec_compatible_with = [], flaky = None, docker_image_version = None, use_login_shell = None, prepare_script = None):
    """Execute an run_tests.py-harness style test under bazel.

    Args:
        name: The name of the test.
        args: The args to supply to the test binary.
        data: Data dependencies.
        size: The size of the test.
        timeout: The test timeout.
        tags: The tags for the test.
        exec_compatible_with: A list of constraint values that must be
            satisifed for the platform.
        flaky: Whether this test is flaky.
        docker_image_version: The docker .current_version file to use for docker containerization.
        use_login_shell: If True, the run_tests.py command will run under a login shell.
        prepare_script: Optional script that will be sourced before run_tests.py runs.
    """

    data = [
        "//tools/bazelify_tests:grpc_repo_archive_with_submodules.tar.gz",
    ] + data

    args = [
        "$(location //tools/bazelify_tests:grpc_repo_archive_with_submodules.tar.gz)",
    ] + args

    srcs = [
        "//tools/bazelify_tests:grpc_run_tests_harness_test.sh",
    ]

    env = {}

    if use_login_shell:
        env["GRPC_RUNTESTS_USE_LOGIN_SHELL"] = "1"

    if prepare_script:
        data = data + [prepare_script]
        env["GRPC_RUNTESTS_PREPARE_SCRIPT"] = "$(location " + prepare_script + ")"

    # Enable ccache by default. This is important for speeding up the C++ cmake build,
    # which isn't very efficient and tends to recompile some source files multiple times.
    # Even though only the local disk cache is enabled (local to the docker container,
    # so will be thrown away after the bazel actions finishes), ccache still speeds up
    # the C++ build significantly.
    # TODO(jtattermusch): find a cleaner way to toggle ccache for builds.
    env["GRPC_BUILD_ENABLE_CCACHE"] = "true"

    _dockerized_sh_test(name = name, srcs = srcs, args = args, data = data, size = size, timeout = timeout, tags = tags, exec_compatible_with = exec_compatible_with, flaky = flaky, docker_image_version = docker_image_version, env = env)

def grpc_run_bazel_distribtest_test(name, args = [], data = [], size = "medium", timeout = None, tags = [], exec_compatible_with = [], flaky = None, docker_image_version = None):
    """Execute bazel distribtest under bazel (an entire bazel build/test will run in a container as a single bazel action)

    Args:
        name: The name of the test.
        args: The args to supply to the test binary.
        data: Data dependencies.
        size: The size of the test.
        timeout: The test timeout.
        tags: The tags for the test.
        exec_compatible_with: A list of constraint values that must be
            satisifed for the platform.
        flaky: Whether this test is flaky.
        docker_image_version: The docker .current_version file to use for docker containerization.
    """

    data = [
        "//tools/bazelify_tests:grpc_repo_archive_with_submodules.tar.gz",
    ] + data

    args = [
        "$(location //tools/bazelify_tests:grpc_repo_archive_with_submodules.tar.gz)",
    ] + args

    srcs = [
        "//tools/bazelify_tests:grpc_run_bazel_distribtest_test.sh",
    ]
    env = {}
    _dockerized_sh_test(name = name, srcs = srcs, args = args, data = data, size = size, timeout = timeout, tags = tags, exec_compatible_with = exec_compatible_with, flaky = flaky, docker_image_version = docker_image_version, env = env)

def grpc_run_cpp_distribtest_test(name, args = [], data = [], size = "medium", timeout = None, tags = [], exec_compatible_with = [], flaky = None, docker_image_version = None):
    """Execute an C++ distribtest under bazel.

    Args:
        name: The name of the test.
        args: The args to supply to the test binary.
        data: Data dependencies.
        size: The size of the test.
        timeout: The test timeout.
        tags: The tags for the test.
        exec_compatible_with: A list of constraint values that must be
            satisifed for the platform.
        flaky: Whether this test is flaky.
        docker_image_version: The docker .current_version file to use for docker containerization.
    """

    data = [
        "//tools/bazelify_tests:grpc_repo_archive_with_submodules.tar.gz",
    ] + data

    args = [
        "$(location //tools/bazelify_tests:grpc_repo_archive_with_submodules.tar.gz)",
    ] + args

    srcs = [
        "//tools/bazelify_tests:grpc_run_cpp_distribtest_test.sh",
    ]

    # TODO(jtattermusch): revisit running docker as root (but currently some distribtests need to install stuff inside the docker container)
    env = {}
    _dockerized_sh_test(name = name, srcs = srcs, args = args, data = data, size = size, timeout = timeout, tags = tags, exec_compatible_with = exec_compatible_with, flaky = flaky, docker_image_version = docker_image_version, env = env, docker_run_as_root = True)

def grpc_run_simple_command_test(name, args = [], data = [], size = "medium", timeout = None, tags = [], exec_compatible_with = [], flaky = None, docker_image_version = None):
    """Execute the specified test command under grpc workspace (and under a docker container)

    Args:
        name: The name of the test.
        args: The command to run.
        data: Data dependencies.
        size: The size of the test.
        timeout: The test timeout.
        tags: The tags for the test.
        exec_compatible_with: A list of constraint values that must be
            satisifed for the platform.
        flaky: Whether this test is flaky.
        docker_image_version: The docker .current_version file to use for docker containerization.
    """

    data = [
        "//tools/bazelify_tests:grpc_repo_archive_with_submodules.tar.gz",
    ] + data

    args = [
        "$(location //tools/bazelify_tests:grpc_repo_archive_with_submodules.tar.gz)",
    ] + args

    srcs = [
        "//tools/bazelify_tests:grpc_run_simple_command_test.sh",
    ]

    env = {}
    _dockerized_sh_test(name = name, srcs = srcs, args = args, data = data, size = size, timeout = timeout, tags = tags, exec_compatible_with = exec_compatible_with, flaky = flaky, docker_image_version = docker_image_version, env = env, docker_run_as_root = False)

def grpc_build_artifact_task(name, timeout = None, artifact_deps = [], tags = [], exec_compatible_with = [], flaky = None, docker_image_version = None, build_script = None):
    """Execute a build artifact task and a corresponding 'build test'


    Args:
        name: The name of the target.
        timeout: The test timeout for the build.
        artifact_deps: List of dependencies on artifacts built by another grpc_build_artifact_task.
        tags: The tags for the target.
        exec_compatible_with: A list of constraint values that must be
            satisifed for the platform.
        flaky: Whether this artifact build is flaky.
        docker_image_version: The docker .current_version file to use for docker containerization.
        build_script: The script that builds the aritfacts.
    """

    out_exitcode_file = str(name + "_exit_code")
    out_build_log = str(name + "_build_log.txt")
    out_archive_name = str(name + ".tar.gz")

    genrule_outs = [
        out_exitcode_file,
        out_build_log,
        out_archive_name,
    ]

    genrule_srcs = [
        "//tools/bazelify_tests:grpc_build_artifact_task.sh",
        "//tools/bazelify_tests:grpc_repo_archive_with_submodules.tar.gz",
        build_script,
    ]

    cmd = "$(location //tools/bazelify_tests:grpc_build_artifact_task.sh) $(location //tools/bazelify_tests:grpc_repo_archive_with_submodules.tar.gz) $(location " + build_script + ") $(location " + out_exitcode_file + ") $(location " + out_build_log + ") $(location " + out_archive_name + ")"

    # for each artifact task we depends on, use the correponding tar.gz as extra src and pass its location as an extra cmdline arg.
    for dep in artifact_deps:
        dep_archive_name = str(dep + ".tar.gz")
        cmd = cmd + " $(location " + dep_archive_name + ")"
        genrule_srcs.append(dep_archive_name)

    _dockerized_genrule(name = name, cmd = cmd, outs = genrule_outs, srcs = genrule_srcs, timeout = timeout, tags = tags, exec_compatible_with = exec_compatible_with, flaky = flaky, docker_image_version = docker_image_version, docker_run_as_root = False)

    test_name = str(name + "_build_test")
    test_srcs = [
        "//tools/bazelify_tests:grpc_build_artifact_task_build_test.sh",
    ]
    test_data = [
        out_exitcode_file,
        out_build_log,
        out_archive_name,
    ]
    test_env = {}
    test_args = [
        "$(location " + out_exitcode_file + ")",
        "$(location " + out_build_log + ")",
        "$(location " + out_archive_name + ")",
    ]
    _dockerized_sh_test(name = test_name, srcs = test_srcs, args = test_args, data = test_data, size = "small", tags = tags, exec_compatible_with = exec_compatible_with, flaky = flaky, docker_image_version = docker_image_version, env = test_env, docker_run_as_root = False)
