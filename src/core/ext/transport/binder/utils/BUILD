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
    name = "transport_stream_receiver",
    srcs = [
        "transport_stream_receiver_impl.cc",
    ],
    hdrs = [
        "transport_stream_receiver.h",
        "transport_stream_receiver_impl.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/synchronization",
    ],
    deps = [
        "//:gpr",
        "//:gpr_base",
        "//:gpr_platform",
        "//src/core/ext/transport/binder/wire_format:transaction",
    ],
)
