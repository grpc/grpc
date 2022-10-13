# Copyright 2022 gRPC authors.
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
)

licenses(["reciprocal"])

package(
    default_visibility = ["//:__subpackages__"],
    features = [
        "layering_check",
    ],
)

# This is needed as a transitionary mechanism to build the src/core targets in
# the top-level BUILD file that have not yet been moved here. Should go away
# once the transition is complete.
exports_files(
    glob(["**"]),
    visibility = ["//:__subpackages__"],
)

grpc_cc_library(
    name = "channel_fwd",
    hdrs = [
        "lib/channel/channel_fwd.h",
    ],
    language = "c++",
)

grpc_cc_library(
    name = "event_engine_common",
    srcs = [
        "lib/event_engine/resolved_address.cc",
        "lib/event_engine/slice.cc",
        "lib/event_engine/slice_buffer.cc",
    ],
    hdrs = [
        "lib/event_engine/handle_containers.h",
        "//:include/grpc/event_engine/slice.h",
        "//:include/grpc/event_engine/slice_buffer.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/hash",
        "absl/strings",
        "absl/utility",
    ],
    visibility = ["@grpc:event_engine_common"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:gpr_platform",
        "//:slice",
        "//:slice_buffer",
        "//:slice_refcount",
    ],
)
