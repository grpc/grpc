# Copyright 2021 The gRPC Authors
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
"""Load dependencies needed to compile and test the grpc python library as a 3rd-party consumer."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@com_github_grpc_grpc//third_party/py:python_configure.bzl", "python_configure")

# buildifier: disable=unnamed-macro
def grpc_python_deps():
    """Loads dependencies for gRPC Python."""
    if "rules_python" not in native.existing_rules():
        http_archive(
            name = "rules_python",
            sha256 = "4f7e2aa1eb9aa722d96498f5ef514f426c1f55161c3c9ae628c857a7128ceb07",
            strip_prefix = "rules_python-1.0.0",
            url = "https://github.com/bazelbuild/rules_python/releases/download/1.0.0/rules_python-1.0.0.tar.gz",
        )

    python_configure(name = "local_config_python")

    native.bind(
        name = "python_headers",
        actual = "@local_config_python//:python_headers",
    )

    if "cython" not in native.existing_rules():
        http_archive(
            name = "cython",
            build_file = "@com_github_grpc_grpc//third_party:cython.BUILD",
            sha256 = "2ec7d66d23d6da2328fb24f5c1bec6c63a59ec2e91027766ab904f417e1078aa",
            strip_prefix = "cython-3.0.11",
            urls = [
                "https://github.com/cython/cython/archive/3.0.11.tar.gz",
            ],
        )
