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

load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_proto_library", "py_grpc_library")

package(default_testonly = 1)

proto_library(
    name = "helloworld_proto",
    srcs = ["helloworld.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
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
    main = "helloworld.py",
    srcs = ["helloworld.py"],
    deps = [
        ":helloworld_py_pb2",
        ":helloworld_py_pb2_grpc",
        ":duration_py_pb2",
        ":timestamp_py_pb2",
    ],
    python_version = "PY3",
)

# Test compatibility of py_proto_library and py_grpc_library rules with
# proto_library targets as deps when the latter use import_prefix and/or
# strip_import_prefix arguments
proto_library(
    name = "helloworld_moved_proto",
    srcs = ["helloworld.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
    import_prefix = "google/cloud",
    strip_import_prefix = ""
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
    main = "helloworld.py",
    srcs = ["helloworld.py"],
    deps = [
        ":helloworld_moved_py_pb2",
        ":helloworld_moved_py_pb2_grpc",
        ":duration_py_pb2",
        ":timestamp_py_pb2",
    ],
    imports = [
        "_virtual_imports/helloworld_moved_proto",
        # The following line allows us to keep helloworld.py file same for both
        # test cases ("import_test" and "import_moved_test") and reduce the code
        # duplication.
        #
        # Without this line, the actual imports in hellowold.py should look
        # like the following:
        #     import google.cloud.helloworld_pb2 as helloworld_pb2
        # instead of:
        #     import helloworld_pb2
        "_virtual_imports/helloworld_moved_proto/google/cloud"
    ],
    python_version = "PY3",
)