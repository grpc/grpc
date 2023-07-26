# Copyright 2017 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_sh_test")

licenses(["notice"])

grpc_sh_test(
    name = "csharp_codegen_simple_test",
    size = "small",
    srcs = ["csharp_codegen_simple_test.sh"],
    data = [
        "simple/expected/HelloworldGrpc.cs",
        "simple/proto/helloworld.proto",
        "//src/compiler:grpc_csharp_plugin",
        "@com_google_protobuf//:protoc",
    ],
    tags = [
        "no_windows",
        "noasan",
        "nomsan",
        "notsan",
        "noubsan",
    ],
    uses_polling = False,
)

grpc_sh_test(
    name = "csharp_codegen_deprecated_test",
    size = "small",
    srcs = ["csharp_codegen_deprecated_test.sh"],
    data = [
        "deprecated/proto/depmethod.proto",
        "deprecated/proto/depnothing.proto",
        "deprecated/proto/depservice.proto",
        "//src/compiler:grpc_csharp_plugin",
        "@com_google_protobuf//:protoc",
    ],
    tags = [
        "no_windows",
        "noasan",
        "nomsan",
        "notsan",
        "noubsan",
    ],
    uses_polling = False,
)

grpc_sh_test(
    name = "csharp_codegen_base_namespace_test",
    size = "small",
    srcs = ["csharp_codegen_base_namespace_test.sh"],
    data = [
        "basenamespace/proto/namespacetest.proto",
        "//src/compiler:grpc_csharp_plugin",
        "@com_google_protobuf//:protoc",
    ],
    tags = [
        "no_windows",
        "noasan",
        "nomsan",
        "notsan",
        "noubsan",
    ],
    uses_polling = False,
)

grpc_sh_test(
    name = "csharp_codegen_nullable_test",
    size = "small",
    srcs = ["csharp_codegen_nullable_test.sh"],
    data = [
        "nullable/proto/nullabletest.proto",
        "//src/compiler:grpc_csharp_plugin",
        "@com_google_protobuf//:protoc",
    ],
    tags = [
        "no_windows",
        "noasan",
        "nomsan",
        "notsan",
        "noubsan",
    ],
    uses_polling = False,
)
