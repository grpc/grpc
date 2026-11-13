# Copyright 2026 gRPC authors.
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

# BUILD file for the autosharding repository when gRPC is used via WORKSPACE.
# The upstream archive has no BUILD files. Under Bzlmod, the BCR module
# provides an equivalent overlay.

licenses(["notice"])

package(
    default_visibility = ["//visibility:public"],
)

proto_library(
    name = "autosharding_proto",
    srcs = ["proto/autosharding/v1/autosharding.proto"],
    deps = [
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)
