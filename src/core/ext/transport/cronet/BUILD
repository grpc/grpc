# gRPC Bazel BUILD file.
#
# Copyright 2016 gRPC authors.
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

licenses(["notice"])  # Apache v2

package(
    default_visibility = ["//visibility:public"],
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

grpc_cc_library(
    name = "grpc_transport_cronet_client_secure",
    srcs = [
        "client/secure/cronet_channel_create.cc",
        "transport/cronet_api_phony.cc",
        "transport/cronet_status.cc",
        "transport/cronet_status.h",
        "transport/cronet_transport.cc",
        "transport/cronet_transport.h",
    ],
    external_deps = [
        "cronet_c_for_grpc",
    ],
    language = "c++",
    public_hdrs = [
        "client/secure/cronet_channel_create.h",
    ],
    deps = [
        "//:grpc_base",
        "//:grpc_transport_chttp2",
    ],
)

grpc_cc_library(
    name = "grpc_cronet_plugin_registry",
    srcs = [
        "plugin_registry/grpc_cronet_plugin_registry.cc",
    ],
    language = "c++",
    deps = [
        ":grpc_transport_cronet_client_secure",
        "//:grpc_base",
        "//:grpc_http_filters",
        "//:grpc_transport_chttp2_client_secure",
    ],
)
