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

load("//bazel:grpc_build_system.bzl", "grpc_package")

licenses(["notice"])

#  TODO(jtattermusch): adjust the visibility
grpc_package(
    name = "tools/bazelify_tests",
    visibility = "public",
)

exports_files([
    "grpc_run_tests_py_test.sh",
    "grpc_run_bazel_distribtest_test.sh",
    "grpc_run_cpp_distribtest_test.sh",
])

genrule(
    name = "grpc_repo_archive",
    srcs =
        [
            "grpc_repo_archive.sh",
        ],
    outs = [
        "grpc_repo_archive.tar.gz",
        "grpc_repo_archive_with_submodules.tar.gz",
    ],
    cmd = "$(location grpc_repo_archive.sh) $(location grpc_repo_archive.tar.gz) $(location grpc_repo_archive_with_submodules.tar.gz)",
    local = True,
    stamp = 1,
    tags = [
        "manual",
    ],
)

alias(
    name = "grpc_repo_archive_with_submodules",
    actual = ":grpc_repo_archive",
)
