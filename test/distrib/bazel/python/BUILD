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

# load("@rules_proto//proto:defs.bzl", "proto_library")
# load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")

# load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")

load(
    "@com_github_grpc_grpc//bazel:python_rules.bzl",
    "py_grpc_library",
    "py_proto_library",
)
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:defs.bzl", "py_library", "py_test")
load("//:python_rules_test.bzl", "python_rules_test_suite")

package(
    default_testonly = 1,
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
