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
            sha256 = "2f5c284fbb4e86045c2632d3573fc006facbca5d1fa02976e89dc0cd5488b590",
            strip_prefix = "rules_python-1.6.3",
            url = "https://github.com/bazel-contrib/rules_python/releases/download/1.6.3/rules_python-1.6.3.tar.gz",
        )

    python_configure(name = "local_config_python")

    native.bind(
        name = "python_headers",
        actual = "@local_config_python//:python_headers",
    )

    # This version should be same as that in G3
    http_archive(
        name = "typing_extensions",
        build_file = "@com_github_grpc_grpc//third_party:typing_extensions.BUILD",
        sha256 = "bf6f56b36d8bc9156e518eb1cc37a146284082fa53522033f772aefbecfd15fc",
        strip_prefix = "typing_extensions-4.12.2",
        url = "https://github.com/python/typing_extensions/archive/4.12.2.tar.gz",
    )

    if "cython" not in native.existing_rules():
        http_archive(
            name = "cython",
            build_file = "@com_github_grpc_grpc//third_party:cython.BUILD",
            sha256 = "72d00515d74df2eaffe3802800448b6055d8d2f2dbc4564fa1e2e1c770a16f20",
            strip_prefix = "cython-3.2.1",
            urls = [
                "https://github.com/cython/cython/archive/3.2.1.tar.gz",
            ],
        )
