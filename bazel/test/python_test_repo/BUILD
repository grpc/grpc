# gRPC Bazel BUILD file.
#
# Copyright 2019 The gRPC authors.
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

load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "@com_github_grpc_grpc//bazel:python_rules.bzl",
    "py_grpc_library",
    "py_proto_library",
)

package(
    default_testonly = 1,
)

proto_library(
    name = "helloworld_proto",
    srcs = ["helloworld.proto"],
    deps = [
        ":hello_dep_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

# Test that .proto files can be located in strict subdiretories of the package.
proto_library(
    name = "hello_dep_proto",
    srcs = ["subdir/hello_dep.proto"],
)

py_proto_library(
    name = "helloworld_py_pb2",
    deps = [":helloworld_proto"],
)

py_grpc_library(
    name = "helloworld_py_pb2_grpc",
    srcs = [":helloworld_proto"],
    deps = [":helloworld_py_pb2"],
)

py_proto_library(
    name = "duration_py_pb2",
    deps = ["@com_google_protobuf//:duration_proto"],
)

py_proto_library(
    name = "timestamp_py_pb2",
    deps = ["@com_google_protobuf//:timestamp_proto"],
)

py_test(
    name = "import_test",
    srcs = ["helloworld.py"],
    main = "helloworld.py",
    python_version = "PY3",
    deps = [
        ":duration_py_pb2",
        ":helloworld_py_pb2",
        ":helloworld_py_pb2_grpc",
        ":timestamp_py_pb2",
    ],
)

# Test compatibility of py_proto_library and py_grpc_library rules with
# proto_library targets as deps when the latter use import_prefix and/or
# strip_import_prefix arguments
#
# See namespaced/upper/example for more encompassing tests.
proto_library(
    name = "helloworld_moved_proto",
    srcs = ["helloworld.proto"],
    import_prefix = "google/cloud",
    strip_import_prefix = "",
    deps = [
        ":hello_dep_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

py_proto_library(
    name = "helloworld_moved_py_pb2",
    deps = [":helloworld_moved_proto"],
)

py_grpc_library(
    name = "helloworld_moved_py_pb2_grpc",
    srcs = [":helloworld_moved_proto"],
    deps = [":helloworld_moved_py_pb2"],
)

py_test(
    name = "import_moved_test",
    srcs = ["helloworld_moved.py"],
    main = "helloworld_moved.py",
    python_version = "PY3",
    deps = [
        ":duration_py_pb2",
        ":helloworld_moved_py_pb2",
        ":helloworld_moved_py_pb2_grpc",
        ":timestamp_py_pb2",
    ],
)

# Test that a py_proto_library wrapping a proto_library in another package can
# be imported from the package that contains the py_proto_library *AND* from
# the package that contains the proto_library.
py_proto_library(
    name = "subpackage_py_pb2",
    deps = ["//in_subpackage:subpackage_proto"],
)

py_test(
    name = "import_from_this_package_subpackage_test",
    srcs = ["import_from_this_package.py"],
    main = "import_from_this_package.py",
    python_version = "PY3",
    deps = [
        ":subpackage_py_pb2",
    ],
)

py_test(
    name = "import_from_proto_library_package_test",
    srcs = ["import_from_proto_library_package.py"],
    main = "import_from_proto_library_package.py",
    python_version = "PY3",
    deps = [
        ":subpackage_py_pb2",
    ],
)

# Test that a py_proto_library can be successfully imported without requiring
# explicit dependencies on unused dependencies of the generated code.
py_test(
    name = "transitive_proto_dep_test",
    srcs = ["transitive_proto_dep.py"],
    main = "transitive_proto_dep.py",
    python_version = "PY3",
    deps = [
        ":helloworld_py_pb2",
    ],
)
