# Copyright 2026 The gRPC Authors
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

load("@com_google_protobuf//bazel/toolchains:proto_lang_toolchain.bzl", "proto_lang_toolchain")

# Used for `py_grpc_library`.
proto_lang_toolchain(
    name = "grpc_python_toolchain",
    plugin = "//src/compiler:grpc_python_plugin",
    command_line = "--grpc_python_out=$(OUT)",
    plugin_format_flag = "--plugin=protoc-gen-grpc_python=%s",
    progress_message = "Generating gRPC Python code for %{label}",
    mnemonic = "ProtocInvocation",
    visibility = ["//visibility:public"],
)

# Used for `py_proto_library` (no plugin).
proto_lang_toolchain(
    name = "protobuf_python_toolchain",
    command_line = "--python_out=$(OUT)",
    mnemonic = "ProtocInvocation",
    progress_message = "Generating Protobuf Python code for %{label}",
    visibility = ["//visibility:public"],
)

# The C++ and Objective-C gRPC codegen rules
# (//bazel:generate_cc.bzl and //bazel:generate_objc.bzl)
# assemble the protoc command line by hand and pass an optional gRPC plugin per target,
# so they consume these toolchains only for their resolved protoc executable
# (ProtoLangToolchainInfo.proto_compiler).

proto_lang_toolchain(
    name = "grpc_cpp_toolchain",
    command_line = "--grpc_cpp_out=$(OUT)",
    progress_message = "Generating gRPC C++ code for %{label}",
    mnemonic = "ProtocInvocation",
    visibility = ["//visibility:public"],
)

proto_lang_toolchain(
    name = "grpc_objective_c_toolchain",
    command_line = "--grpc_objective_c_out=$(OUT)",
    progress_message = "Generating gRPC Objective-C code for %{label}",
    mnemonic = "ProtocInvocation",
    visibility = ["//visibility:public"],
)
