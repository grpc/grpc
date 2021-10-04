# Copyright 2021 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library")

licenses(["notice"])

package(
    default_visibility = ["//visibility:public"],
    features = [
        "layering_check",
    ],
)

grpc_cc_library(
    name = "security_policy_interface",
    hdrs = [
        "security_policy.h",
    ],
    deps = [
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "internal_only_security_policy",
    srcs = [
        "internal_only_security_policy.cc",
    ],
    hdrs = [
        "internal_only_security_policy.h",
    ],
    deps = [
        ":security_policy_interface",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "untrusted_security_policy",
    srcs = [
        "untrusted_security_policy.cc",
    ],
    hdrs = [
        "untrusted_security_policy.h",
    ],
    deps = [
        ":security_policy_interface",
        "//:gpr_platform",
    ],
)
