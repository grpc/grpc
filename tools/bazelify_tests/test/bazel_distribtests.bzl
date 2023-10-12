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
Generates portability tests.
"""

load("supported_bazel_versions.bzl", "SUPPORTED_BAZEL_VERSIONS")
load("//tools/bazelify_tests:build_defs.bzl", "grpc_run_bazel_distribtest_test")

_TEST_SHARDS = [
    "buildtest",
    "distribtest_cpp",
    "distribtest_python",
]

def generate_bazel_distribtests(name):
    """Generates the bazel distribtests.

    Args:
        name: Name of the test suite that will be generated.
    """
    test_names = []

    for bazel_version in SUPPORTED_BAZEL_VERSIONS:
        for shard_name in _TEST_SHARDS:
            test_name = "bazel_distribtest_%s_%s" % (bazel_version, shard_name)
            grpc_run_bazel_distribtest_test(
                name = test_name,
                size = "enormous",
                args = [bazel_version, shard_name],
                docker_image_version = "tools/dockerfile/test/bazel.current_version",
            )
            test_names.append(test_name)

    # Generate test suite that allows easily running all bazel distribtests.
    native.test_suite(
        name = name,
        tests = [(":%s" % test_name) for test_name in test_names],
    )
