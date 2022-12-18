# gRPC Bazel BUILD file.
#
# Copyright 2020 The gRPC authors.
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

_IMPORT_PREFIX = "foo/bar"

_STRIP_PREFIX = "/%s" % package_name()

proto_library(
    name = "import_no_strip_proto",
    srcs = ["namespaced_example.proto"],
    import_prefix = _IMPORT_PREFIX,
    strip_import_prefix = None,
)

proto_library(
    name = "import_strip_proto",
    srcs = ["namespaced_example.proto"],
    import_prefix = _IMPORT_PREFIX,
    strip_import_prefix = _STRIP_PREFIX,
)

proto_library(
    name = "no_import_no_strip_proto",
    srcs = ["namespaced_example.proto"],
    import_prefix = None,
    strip_import_prefix = None,
)

proto_library(
    name = "no_import_strip_proto",
    srcs = ["namespaced_example.proto"],
    import_prefix = None,
    strip_import_prefix = _STRIP_PREFIX,
)

py_proto_library(
    name = "import_no_strip_py_pb2",
    deps = ["import_no_strip_proto"],
)

py_grpc_library(
    name = "import_no_strip_py_pb2_grpc",
    srcs = ["import_no_strip_proto"],
    deps = ["import_no_strip_py_pb2"],
)

py_proto_library(
    name = "import_strip_py_pb2",
    deps = ["import_strip_proto"],
)

py_grpc_library(
    name = "import_strip_py_pb2_grpc",
    srcs = ["import_strip_proto"],
    deps = ["import_strip_py_pb2"],
)

py_proto_library(
    name = "no_import_no_strip_py_pb2",
    deps = ["no_import_no_strip_proto"],
)

py_grpc_library(
    name = "no_import_no_strip_py_pb2_grpc",
    srcs = ["no_import_no_strip_proto"],
    deps = ["no_import_no_strip_py_pb2"],
)

py_proto_library(
    name = "no_import_strip_py_pb2",
    deps = ["no_import_strip_proto"],
)

py_grpc_library(
    name = "no_import_strip_py_pb2_grpc",
    srcs = ["no_import_strip_proto"],
    deps = ["no_import_strip_py_pb2"],
)

#################
# Namespace Tests
#################

# Most examples with protos have all proto packages rooted at the workspace root. i.e. google/api has
# a directory structure:
# - WORKSPACE
# - /google
#   - /api
#     - files.proto
#
# But if you can't anchor the protos at the root, you have to use strip and import prefixes. This results
# in the following directory layout for python, which needs to properly be added to the imports.
#
# No Import or Strip (Can't compile if there are any proto dependencies)
# bazel-bin/namespaced/upper/example/namespaced_example_pb2.py
#
# No import Prefix (Can't compile if there are any proto dependencies)
# bazel-bin/namespaced/upper/example/_virtual_imports/namespaced_example_proto/namespaced_example_pb2.py
#
# No strip prefix (Can't compile if there are any proto dependencies)
# bazel-bin/namespaced/upper/example/_virtual_imports/namespaced_example_proto/upper/example/namespaced/upper/example/namespaced_example_pb2.py
#
# Both Import and Strip
# bazel-bin/namespaced/upper/example/_virtual_imports/namespaced_example_proto/upper/example/namespaced_example_pb2.py

py_test(
    name = "import_no_strip_test",
    srcs = ["import_no_strip_test.py"],
    main = "import_no_strip_test.py",
    python_version = "PY3",
    deps = [
        ":import_no_strip_py_pb2",
        ":import_no_strip_py_pb2_grpc",
    ],
)

py_test(
    name = "import_strip_test",
    srcs = ["import_strip_test.py"],
    main = "import_strip_test.py",
    python_version = "PY3",
    deps = [
        ":import_strip_py_pb2",
        ":import_strip_py_pb2_grpc",
    ],
)

py_test(
    name = "no_import_no_strip_test",
    srcs = ["no_import_no_strip_test.py"],
    main = "no_import_no_strip_test.py",
    python_version = "PY3",
    deps = [
        ":no_import_no_strip_py_pb2",
        ":no_import_no_strip_py_pb2_grpc",
    ],
)

py_test(
    name = "no_import_strip_test",
    srcs = ["no_import_strip_test.py"],
    main = "no_import_strip_test.py",
    python_version = "PY3",
    deps = [
        ":no_import_strip_py_pb2",
        ":no_import_strip_py_pb2_grpc",
    ],
)
