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
    "grpc_proto_plugin",
)

package(
    default_visibility = ["//visibility:public"],
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

licenses(["notice"])

exports_files(["LICENSE"])

grpc_cc_library(
    name = "proto_parser_helper",
    srcs = ["proto_parser_helper.cc"],
    hdrs = ["proto_parser_helper.h"],
    language = "c++",
)

grpc_cc_library(
    name = "grpc_plugin_support",
    srcs = [
        "cpp_generator.cc",
        "csharp_generator.cc",
        "node_generator.cc",
        "objective_c_generator.cc",
        "php_generator.cc",
        "python_generator.cc",
        "ruby_generator.cc",
    ],
    hdrs = [
        "config.h",
        "config_protobuf.h",
        "cpp_generator.h",
        "cpp_generator_helpers.h",
        "cpp_plugin.h",
        "csharp_generator.h",
        "csharp_generator_helpers.h",
        "generator_helpers.h",
        "node_generator.h",
        "node_generator_helpers.h",
        "objective_c_generator.h",
        "objective_c_generator_helpers.h",
        "php_generator.h",
        "php_generator_helpers.h",
        "protobuf_plugin.h",
        "python_generator.h",
        "python_generator_helpers.h",
        "python_private_generator.h",
        "ruby_generator.h",
        "ruby_generator_helpers-inl.h",
        "ruby_generator_map-inl.h",
        "ruby_generator_string-inl.h",
        "schema_interface.h",
    ],
    external_deps = [
        "protobuf_clib",
        "protobuf",
    ],
    language = "c++",
    deps = [
        "proto_parser_helper",
        "//:grpc++_config_proto",
    ],
)

grpc_proto_plugin(
    name = "grpc_cpp_plugin",
    srcs = ["cpp_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_csharp_plugin",
    srcs = ["csharp_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_node_plugin",
    srcs = ["node_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_objective_c_plugin",
    srcs = ["objective_c_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_php_plugin",
    srcs = ["php_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_python_plugin",
    srcs = ["python_plugin.cc"],
    deps = [":grpc_plugin_support"],
)

grpc_proto_plugin(
    name = "grpc_ruby_plugin",
    srcs = ["ruby_plugin.cc"],
    deps = [":grpc_plugin_support"],
)
