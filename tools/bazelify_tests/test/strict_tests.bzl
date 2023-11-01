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
Generates a suite of strict build tests, to minimize runtime.
"""

load("//tools/bazelify_tests:build_defs.bzl", "grpc_run_simple_command_test")

def _safe_target_name(name):
    """Returns a sanitized name for a target"""
    return name.replace(":", "").replace("/", "_").replace(".", "").replace(" ", "_")

def generate_strict_tests(name = ""):
    """Generates a suite of strict build tests, to minimize runtime.

    Args:
        name: unused (required by buildifier)
    """
    strict_warning_jobs = []

    for source in [
        ":all //src/core/... //src/compiler/... //examples/... -//examples/android/binder/...",
        "//test/... -//test/core/... -//test/cpp/...",
        "//test/core/end2end/...",
        "//test/core/... -//test/core/end2end/...",
        "//test/cpp/... -//test/cpp/end2end/...",
        "//test/cpp/end2end/xds/...",
        "//test/cpp/end2end/... -//test/cpp/end2end/xds/...",
    ]:
        test_name = "bazel_build_with_strict_warnings_linux_" + _safe_target_name(source)
        strict_warning_jobs.append(":" + test_name)
        grpc_run_simple_command_test(
            name = test_name,
            size = "enormous",
            args = ["tools/bazelify_tests/test/bazel_build_with_strict_warnings_linux.sh " + source],
            docker_image_version = "tools/dockerfile/test/bazel.current_version",
        )

    native.test_suite(
        name = "bazel_build_with_strict_warnings_linux",
        tests = strict_warning_jobs,
    )
