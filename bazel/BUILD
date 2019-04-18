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

licenses(["notice"])  # Apache v2

package(default_visibility = ["//:__subpackages__"])

load(":cc_grpc_library.bzl", "cc_grpc_library")

proto_library(
    name = "well_known_protos_list",
    srcs = ["@com_google_protobuf//:well_known_protos"],
)

cc_grpc_library(
    name = "well_known_protos",
    srcs = "well_known_protos_list",
    proto_only = True,
    deps = [],
)
