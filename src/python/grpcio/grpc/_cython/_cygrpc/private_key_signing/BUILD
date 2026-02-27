# Copyright 2025 gRPC authors.
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

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
)

licenses(["reciprocal"])

package(
    default_visibility = ["//:__subpackages__"],
    features = [
        "layering_check",
        "parse_headers",
    ],
)

grpc_cc_library(
    name = "private_key_signer_py_wrapper",
    srcs = ["private_key_signer_py_wrapper.cc"],
    hdrs = ["private_key_signer_py_wrapper.h"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
    ],
    visibility = [
        "//src/core/grpcio/grpc/_cython:__subpackages__",
    ],
    deps = [
        "//:gpr",
        "//:grpc_base",
        # "@local_config_python//:python_headers",
        "@rules_python//python/cc:current_py_cc_headers",
        "@rules_python//python/cc:current_py_cc_libs",
    ],
)
