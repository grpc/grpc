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

def grpc_python_deps():
    # protobuf binds to the name "six", so we can't use it here.
    # See https://github.com/bazelbuild/bazel/issues/1952 for why bind is
    # horrible.
    if "six" not in native.existing_rules():
        http_archive(
            name = "six",
            build_file = "@com_github_grpc_grpc//third_party:six.BUILD",
            sha256 = "1e61c37477a1626458e36f7b1d82aa5c9b094fa4802892072e49de9c60c4c926",
            urls = ["https://files.pythonhosted.org/packages/71/39/171f1c67cd00715f190ba0b100d606d440a28c93c7714febeca8b79af85e/six-1.16.0.tar.gz"],
        )

    if "enum34" not in native.existing_rules():
        http_archive(
            name = "enum34",
            build_file = "@com_github_grpc_grpc//third_party:enum34.BUILD",
            strip_prefix = "enum34-1.1.6",
            sha256 = "8ad8c4783bf61ded74527bffb48ed9b54166685e4230386a9ed9b1279e2df5b1",
            urls = ["https://files.pythonhosted.org/packages/bf/3e/31d502c25302814a7c2f1d3959d2a3b3f78e509002ba91aea64993936876/enum34-1.1.6.tar.gz"],
        )

    if "futures" not in native.existing_rules():
        http_archive(
            name = "futures",
            build_file = "@com_github_grpc_grpc//third_party:futures.BUILD",
            strip_prefix = "futures-3.3.0",
            sha256 = "7e033af76a5e35f58e56da7a91e687706faf4e7bdfb2cbc3f2cca6b9bcda9794",
            urls = ["https://files.pythonhosted.org/packages/47/04/5fc6c74ad114032cd2c544c575bffc17582295e9cd6a851d6026ab4b2c00/futures-3.3.0.tar.gz"],
        )

    if "io_bazel_rules_python" not in native.existing_rules():
        http_archive(
            name = "io_bazel_rules_python",
            url = "https://github.com/bazelbuild/rules_python/releases/download/0.4.0/rules_python-0.4.0.tar.gz",
            sha256 = "954aa89b491be4a083304a2cb838019c8b8c3720a7abb9c4cb81ac7a24230cea",
            patches = ["//third_party:rules_python.patch"],
            patch_args = ["-p1"],
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
            sha256 = "e2e38e1f0572ca54d6085df3dec8b607d20e81515fb80215aed19c81e8fe2079",
            strip_prefix = "cython-0.29.21",
            urls = [
                "https://github.com/cython/cython/archive/0.29.21.tar.gz",
            ],
        )
