# Copyright 2020 gRPC authors.
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

licenses(["notice"])  # 3-clause BSD

load("@grpc_python_dependencies//:requirements.bzl", "requirement")

py_binary(
    name = "alts_server",
    srcs = [
        "alts_server.py",
        "demo_pb2.py",
        "demo_pb2_grpc.py",
        "server.py",
    ],
    main = "alts_server.py",
    python_version = "PY3",
    srcs_version = "PY2AND3",
    deps = [
        "//src/python/grpcio/grpc:grpcio",
    ],
)

py_binary(
    name = "alts_client",
    srcs = [
        "alts_client.py",
        "client.py",
        "demo_pb2.py",
        "demo_pb2_grpc.py",
    ],
    main = "alts_client.py",
    python_version = "PY3",
    srcs_version = "PY2AND3",
    deps = [
        "//src/python/grpcio/grpc:grpcio",
    ],
)
