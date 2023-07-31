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
    "grpc_generate_one_off_internal_targets",
    "grpc_upb_proto_library",
    "grpc_upb_proto_reflection_library",
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
    name = "slice_cast",
    hdrs = [
        "//:include/grpc/event_engine/internal/slice_cast.h",
    ],
)

grpc_cc_library(
    name = "event_engine_common",
    srcs = [
        "lib/event_engine/event_engine.cc",
        "lib/event_engine/resolved_address.cc",
        "lib/event_engine/slice.cc",
        "lib/event_engine/slice_buffer.cc",
    ],
    hdrs = [
        "lib/event_engine/handle_containers.h",
        "lib/event_engine/resolved_address_internal.h",
        "//:include/grpc/event_engine/slice.h",
        "//:include/grpc/event_engine/slice_buffer.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/hash",
        "absl/strings",
        "absl/utility",
    ],
    deps = [
        "resolved_address",
        "slice",
        "slice_buffer",
        "slice_cast",
        "slice_refcount",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "transport_fwd",
    hdrs = [
        "lib/transport/transport_fwd.h",
    ],
    language = "c++",
)

grpc_cc_library(
    name = "atomic_utils",
    language = "c++",
    public_hdrs = ["lib/gprpp/atomic_utils.h"],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "metadata_compression_traits",
    hdrs = [
        "lib/transport/metadata_compression_traits.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "experiments",
    srcs = [
        "lib/experiments/config.cc",
        "lib/experiments/experiments.cc",
    ],
    hdrs = [
        "lib/experiments/config.h",
        "lib/experiments/experiments.h",
    ],
    defines = select({
        "//:grpc_experiments_are_final": ["GRPC_EXPERIMENTS_ARE_FINAL"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/functional:any_invocable",
        "absl/strings",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    visibility = ["@grpc:grpc_experiments"],
    deps = [
        "no_destruct",
        "//:config_vars",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "init_internally",
    srcs = ["lib/surface/init_internally.cc"],
    hdrs = ["lib/surface/init_internally.h"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "useful",
    hdrs = ["lib/gpr/useful.h"],
    external_deps = [
        "absl/strings",
        "absl/types:variant",
    ],
    language = "c++",
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "examine_stack",
    srcs = [
        "lib/gprpp/examine_stack.cc",
    ],
    hdrs = [
        "lib/gprpp/examine_stack.h",
    ],
    external_deps = ["absl/types:optional"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "gpr_atm",
    srcs = [
        "lib/gpr/atm.cc",
    ],
    language = "c++",
    public_hdrs = [
        "//:include/grpc/support/atm.h",
        "//:include/grpc/support/atm_gcc_atomic.h",
        "//:include/grpc/support/atm_gcc_sync.h",
        "//:include/grpc/support/atm_windows.h",
        "//:include/grpc/impl/codegen/atm.h",
        "//:include/grpc/impl/codegen/atm_gcc_atomic.h",
        "//:include/grpc/impl/codegen/atm_gcc_sync.h",
        "//:include/grpc/impl/codegen/atm_windows.h",
    ],
    deps = [
        "useful",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "gpr_manual_constructor",
    srcs = [],
    hdrs = [
        "lib/gprpp/manual_constructor.h",
    ],
    language = "c++",
    deps = [
        "construct_destruct",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "gpr_spinlock",
    srcs = [],
    hdrs = [
        "lib/gpr/spinlock.h",
    ],
    language = "c++",
    deps = [
        "gpr_atm",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "gpr_log_internal",
    hdrs = [
        "lib/gpr/log_internal.h",
    ],
    language = "c++",
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "env",
    srcs = [
        "lib/gprpp/linux/env.cc",
        "lib/gprpp/posix/env.cc",
        "lib/gprpp/windows/env.cc",
    ],
    hdrs = [
        "lib/gprpp/env.h",
    ],
    external_deps = ["absl/types:optional"],
    deps = [
        "tchar",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "chunked_vector",
    hdrs = ["lib/gprpp/chunked_vector.h"],
    deps = [
        "arena",
        "gpr_manual_constructor",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "construct_destruct",
    language = "c++",
    public_hdrs = ["lib/gprpp/construct_destruct.h"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "status_helper",
    srcs = [
        "lib/gprpp/status_helper.cc",
    ],
    hdrs = [
        "lib/gprpp/status_helper.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/strings:cord",
        "absl/time",
        "absl/types:optional",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "percent_encoding",
        "slice",
        "//:debug_location",
        "//:google_rpc_status_upb",
        "//:gpr",
        "//:protobuf_any_upb",
    ],
)

grpc_cc_library(
    name = "unique_type_name",
    hdrs = ["lib/gprpp/unique_type_name.h"],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "useful",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "validation_errors",
    srcs = [
        "lib/gprpp/validation_errors.cc",
    ],
    hdrs = [
        "lib/gprpp/validation_errors.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
    ],
    language = "c++",
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "overload",
    language = "c++",
    public_hdrs = ["lib/gprpp/overload.h"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "match",
    external_deps = ["absl/types:variant"],
    language = "c++",
    public_hdrs = ["lib/gprpp/match.h"],
    deps = [
        "overload",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "table",
    external_deps = [
        "absl/meta:type_traits",
        "absl/utility",
    ],
    language = "c++",
    public_hdrs = ["lib/gprpp/table.h"],
    deps = [
        "bitset",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "packed_table",
    hdrs = ["lib/gprpp/packed_table.h"],
    language = "c++",
    deps = [
        "sorted_pack",
        "table",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "bitset",
    language = "c++",
    public_hdrs = ["lib/gprpp/bitset.h"],
    deps = [
        "useful",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "no_destruct",
    language = "c++",
    public_hdrs = ["lib/gprpp/no_destruct.h"],
    deps = [
        "construct_destruct",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "tchar",
    srcs = [
        "lib/gprpp/tchar.cc",
    ],
    hdrs = [
        "lib/gprpp/tchar.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "poll",
    language = "c++",
    public_hdrs = [
        "lib/promise/poll.h",
    ],
    deps = [
        "construct_destruct",
        "//:gpr",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "map_pipe",
    external_deps = ["absl/status"],
    language = "c++",
    public_hdrs = [
        "lib/promise/map_pipe.h",
    ],
    deps = [
        "for_each",
        "map",
        "pipe",
        "poll",
        "promise_factory",
        "promise_trace",
        "try_seq",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "1999",
    srcs = [
        "lib/promise/party.cc",
    ],
    hdrs = [
        "lib/promise/party.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "c++",
    deps = [
        "activity",
        "arena",
        "construct_destruct",
        "context",
        "promise_factory",
        "promise_trace",
        "ref_counted",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_trace",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "context",
    language = "c++",
    public_hdrs = [
        "lib/promise/context.h",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "map",
    language = "c++",
    public_hdrs = ["lib/promise/map.h"],
    deps = [
        "poll",
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "sleep",
    srcs = [
        "lib/promise/sleep.cc",
    ],
    hdrs = [
        "lib/promise/sleep.h",
    ],
    external_deps = ["absl/status"],
    deps = [
        "activity",
        "context",
        "default_event_engine",
        "poll",
        "time",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "arena_promise",
    external_deps = ["absl/meta:type_traits"],
    language = "c++",
    public_hdrs = [
        "lib/promise/arena_promise.h",
    ],
    deps = [
        "arena",
        "construct_destruct",
        "context",
        "poll",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "promise_like",
    external_deps = ["absl/meta:type_traits"],
    language = "c++",
    public_hdrs = [
        "lib/promise/detail/promise_like.h",
    ],
    deps = [
        "poll",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "cancel_callback",
    language = "c++",
    public_hdrs = [
        "lib/promise/cancel_callback.h",
    ],
    deps = [
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "promise_factory",
    external_deps = ["absl/meta:type_traits"],
    language = "c++",
    public_hdrs = [
        "lib/promise/detail/promise_factory.h",
    ],
    deps = [
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "if",
    external_deps = [
        "absl/status:statusor",
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = ["lib/promise/if.h"],
    deps = [
        "construct_destruct",
        "poll",
        "promise_factory",
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "promise_status",
    external_deps = [
        "absl/status",
        "absl/status:statusor",
    ],
    language = "c++",
    public_hdrs = [
        "lib/promise/detail/status.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "race",
    language = "c++",
    public_hdrs = ["lib/promise/race.h"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "prioritized_race",
    language = "c++",
    public_hdrs = ["lib/promise/prioritized_race.h"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "loop",
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/types:variant",
    ],
    language = "c++",
    public_hdrs = [
        "lib/promise/loop.h",
    ],
    deps = [
        "construct_destruct",
        "poll",
        "promise_factory",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "basic_join",
    external_deps = ["absl/utility"],
    language = "c++",
    public_hdrs = [
        "lib/promise/detail/basic_join.h",
    ],
    deps = [
        "bitset",
        "construct_destruct",
        "poll",
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "join",
    external_deps = ["absl/meta:type_traits"],
    language = "c++",
    public_hdrs = [
        "lib/promise/join.h",
    ],
    deps = [
        "basic_join",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "try_join",
    external_deps = [
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
    ],
    language = "c++",
    public_hdrs = [
        "lib/promise/try_join.h",
    ],
    deps = [
        "basic_join",
        "poll",
        "promise_status",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "switch",
    language = "c++",
    public_hdrs = [
        "lib/promise/detail/switch.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "basic_seq",
    external_deps = [
        "absl/meta:type_traits",
        "absl/utility",
    ],
    language = "c++",
    public_hdrs = [
        "lib/promise/detail/basic_seq.h",
    ],
    deps = [
        "construct_destruct",
        "poll",
        "promise_factory",
        "promise_like",
        "switch",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "seq",
    language = "c++",
    public_hdrs = [
        "lib/promise/seq.h",
    ],
    deps = [
        "basic_seq",
        "poll",
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "try_seq",
    external_deps = [
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
    ],
    language = "c++",
    public_hdrs = [
        "lib/promise/try_seq.h",
    ],
    deps = [
        "basic_seq",
        "poll",
        "promise_like",
        "promise_status",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "activity",
    srcs = [
        "lib/promise/activity.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = [
        "lib/promise/activity.h",
    ],
    deps = [
        "atomic_utils",
        "construct_destruct",
        "context",
        "no_destruct",
        "poll",
        "promise_factory",
        "promise_status",
        "//:gpr",
        "//:orphanable",
    ],
)

grpc_cc_library(
    name = "exec_ctx_wakeup_scheduler",
    hdrs = [
        "lib/promise/exec_ctx_wakeup_scheduler.h",
    ],
    external_deps = ["absl/status"],
    language = "c++",
    deps = [
        "closure",
        "error",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_wakeup_scheduler",
    hdrs = [
        "lib/promise/event_engine_wakeup_scheduler.h",
    ],
    language = "c++",
    deps = [
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "wait_set",
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/hash",
    ],
    language = "c++",
    public_hdrs = [
        "lib/promise/wait_set.h",
    ],
    deps = [
        "activity",
        "poll",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "latch",
    external_deps = ["absl/strings"],
    language = "c++",
    public_hdrs = [
        "lib/promise/latch.h",
    ],
    deps = [
        "activity",
        "poll",
        "promise_trace",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "interceptor_list",
    hdrs = [
        "lib/promise/interceptor_list.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    deps = [
        "arena",
        "construct_destruct",
        "context",
        "poll",
        "promise_factory",
        "promise_trace",
        "//:debug_location",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "pipe",
    hdrs = [
        "lib/promise/pipe.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "activity",
        "arena",
        "context",
        "if",
        "interceptor_list",
        "map",
        "poll",
        "promise_trace",
        "seq",
        "//:debug_location",
        "//:gpr",
        "//:grpc_trace",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "promise_trace",
    srcs = [
        "lib/promise/trace.cc",
    ],
    hdrs = [
        "lib/promise/trace.h",
    ],
    language = "c++",
    deps = [
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "mpsc",
    hdrs = [
        "lib/promise/mpsc.h",
    ],
    external_deps = ["absl/base:core_headers"],
    language = "c++",
    deps = [
        "activity",
        "poll",
        "ref_counted",
        "wait_set",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "for_each",
    external_deps = [
        "absl/status",
        "absl/strings",
    ],
    language = "c++",
    public_hdrs = ["lib/promise/for_each.h"],
    deps = [
        "activity",
        "construct_destruct",
        "poll",
        "promise_factory",
        "promise_trace",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "ref_counted",
    language = "c++",
    public_hdrs = ["lib/gprpp/ref_counted.h"],
    deps = [
        "atomic_utils",
        "//:debug_location",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "dual_ref_counted",
    language = "c++",
    public_hdrs = ["lib/gprpp/dual_ref_counted.h"],
    deps = [
        "//:debug_location",
        "//:gpr",
        "//:orphanable",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "uuid_v4",
    srcs = ["lib/gprpp/uuid_v4.cc"],
    external_deps = ["absl/strings:str_format"],
    language = "c++",
    public_hdrs = ["lib/gprpp/uuid_v4.h"],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "handshaker_factory",
    language = "c++",
    public_hdrs = [
        "lib/transport/handshaker_factory.h",
    ],
    deps = [
        "channel_args",
        "iomgr_fwd",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "handshaker_registry",
    srcs = [
        "lib/transport/handshaker_registry.cc",
    ],
    language = "c++",
    public_hdrs = [
        "lib/transport/handshaker_registry.h",
    ],
    deps = [
        "channel_args",
        "handshaker_factory",
        "iomgr_fwd",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "tcp_connect_handshaker",
    srcs = [
        "lib/transport/tcp_connect_handshaker.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = [
        "lib/transport/tcp_connect_handshaker.h",
    ],
    deps = [
        "channel_args",
        "channel_args_endpoint_config",
        "closure",
        "error",
        "handshaker_factory",
        "handshaker_registry",
        "iomgr_fwd",
        "pollset_set",
        "resolved_address",
        "slice",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:handshaker",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "channel_creds_registry",
    hdrs = [
        "lib/security/credentials/channel_creds_registry.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "json",
        "json_args",
        "ref_counted",
        "validation_errors",
        "//:gpr_platform",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "event_engine_memory_allocator",
    srcs = [
        "lib/event_engine/memory_allocator.cc",
    ],
    hdrs = [
        "//:include/grpc/event_engine/internal/memory_allocator_impl.h",
        "//:include/grpc/event_engine/memory_allocator.h",
        "//:include/grpc/event_engine/memory_request.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "slice",
        "slice_refcount",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_memory_allocator_factory",
    hdrs = [
        "lib/event_engine/memory_allocator_factory.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "event_engine_memory_allocator",
        "memory_quota",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "memory_quota",
    srcs = [
        "lib/resource_quota/memory_quota.cc",
    ],
    hdrs = [
        "lib/resource_quota/memory_quota.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_set",
        "absl/status",
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "activity",
        "basic_seq",
        "event_engine_memory_allocator",
        "exec_ctx_wakeup_scheduler",
        "experiments",
        "loop",
        "map",
        "periodic_update",
        "poll",
        "race",
        "resource_quota_trace",
        "seq",
        "time",
        "useful",
        "//:gpr",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "periodic_update",
    srcs = [
        "lib/resource_quota/periodic_update.cc",
    ],
    hdrs = [
        "lib/resource_quota/periodic_update.h",
    ],
    external_deps = ["absl/functional:function_ref"],
    deps = [
        "time",
        "useful",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "arena",
    srcs = [
        "lib/resource_quota/arena.cc",
    ],
    hdrs = [
        "lib/resource_quota/arena.h",
    ],
    external_deps = [
        "absl/meta:type_traits",
        "absl/utility",
    ],
    visibility = [
        "@grpc:alt_grpc_base_legacy",
    ],
    deps = [
        "construct_destruct",
        "context",
        "event_engine_memory_allocator",
        "memory_quota",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "thread_quota",
    srcs = [
        "lib/resource_quota/thread_quota.cc",
    ],
    hdrs = [
        "lib/resource_quota/thread_quota.h",
    ],
    external_deps = ["absl/base:core_headers"],
    deps = [
        "ref_counted",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "resource_quota_trace",
    srcs = [
        "lib/resource_quota/trace.cc",
    ],
    hdrs = [
        "lib/resource_quota/trace.h",
    ],
    deps = [
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "resource_quota",
    srcs = [
        "lib/resource_quota/resource_quota.cc",
    ],
    hdrs = [
        "lib/resource_quota/resource_quota.h",
    ],
    external_deps = ["absl/strings"],
    visibility = [
        "@grpc:alt_grpc_base_legacy",
    ],
    deps = [
        "memory_quota",
        "ref_counted",
        "thread_quota",
        "useful",
        "//:cpp_impl_of",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "slice_refcount",
    srcs = [
        "lib/slice/slice_refcount.cc",
    ],
    hdrs = [
        "lib/slice/slice_refcount.h",
    ],
    public_hdrs = [
        "//:include/grpc/slice.h",
    ],
    deps = [
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "slice",
    srcs = [
        "lib/slice/slice.cc",
        "lib/slice/slice_string_helpers.cc",
    ],
    hdrs = [
        "lib/slice/slice.h",
        "lib/slice/slice_internal.h",
        "lib/slice/slice_string_helpers.h",
        "//:include/grpc/slice.h",
    ],
    external_deps = [
        "absl/hash",
        "absl/strings",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "slice_cast",
        "slice_refcount",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "slice_buffer",
    srcs = [
        "lib/slice/slice_buffer.cc",
    ],
    hdrs = [
        "lib/slice/slice_buffer.h",
        "//:include/grpc/slice_buffer.h",
    ],
    deps = [
        "slice",
        "slice_refcount",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "error",
    srcs = [
        "lib/iomgr/error.cc",
    ],
    hdrs = [
        "lib/iomgr/error.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings:str_format",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "gpr_spinlock",
        "slice",
        "slice_refcount",
        "status_helper",
        "strerror",
        "useful",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "closure",
    srcs = [
        "lib/iomgr/closure.cc",
    ],
    hdrs = [
        "lib/iomgr/closure.h",
    ],
    external_deps = ["absl/strings:str_format"],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "error",
        "gpr_manual_constructor",
        "//:debug_location",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "time",
    srcs = [
        "lib/gprpp/time.cc",
    ],
    hdrs = [
        "lib/gprpp/time.h",
    ],
    external_deps = [
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    deps = [
        "no_destruct",
        "useful",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "iomgr_port",
    hdrs = [
        "lib/iomgr/port.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "iomgr_fwd",
    hdrs = [
        "lib/iomgr/iomgr_fwd.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_sockaddr",
    srcs = [
        "lib/iomgr/sockaddr_utils_posix.cc",
        "lib/iomgr/socket_utils_windows.cc",
    ],
    hdrs = [
        "lib/iomgr/sockaddr.h",
        "lib/iomgr/sockaddr_posix.h",
        "lib/iomgr/sockaddr_windows.h",
        "lib/iomgr/socket_utils.h",
    ],
    deps = [
        "iomgr_port",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "avl",
    hdrs = [
        "lib/avl/avl.h",
    ],
    deps = [
        "useful",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "time_averaged_stats",
    srcs = ["lib/gprpp/time_averaged_stats.cc"],
    hdrs = [
        "lib/gprpp/time_averaged_stats.h",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "forkable",
    srcs = [
        "lib/event_engine/forkable.cc",
    ],
    hdrs = [
        "lib/event_engine/forkable.h",
    ],
    external_deps = ["absl/base:core_headers"],
    deps = [
        "no_destruct",
        "//:config_vars",
        "//:gpr",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_poller",
    hdrs = [
        "lib/event_engine/poller.h",
    ],
    external_deps = ["absl/functional:function_ref"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_time_util",
    srcs = ["lib/event_engine/time_util.cc"],
    hdrs = ["lib/event_engine/time_util.h"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_work_queue",
    hdrs = [
        "lib/event_engine/work_queue/work_queue.h",
    ],
    external_deps = ["absl/functional:any_invocable"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "event_engine_basic_work_queue",
    srcs = [
        "lib/event_engine/work_queue/basic_work_queue.cc",
    ],
    hdrs = [
        "lib/event_engine/work_queue/basic_work_queue.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:any_invocable",
    ],
    deps = [
        "common_event_engine_closures",
        "event_engine_work_queue",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "common_event_engine_closures",
    hdrs = ["lib/event_engine/common_closures.h"],
    external_deps = ["absl/functional:any_invocable"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_timer",
    srcs = [
        "lib/event_engine/posix_engine/timer.cc",
        "lib/event_engine/posix_engine/timer_heap.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/timer.h",
        "lib/event_engine/posix_engine/timer_heap.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/types:optional",
    ],
    deps = [
        "time",
        "time_averaged_stats",
        "useful",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "event_engine_thread_local",
    srcs = ["lib/event_engine/thread_local.cc"],
    hdrs = ["lib/event_engine/thread_local.h"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "event_engine_thread_pool",
    srcs = [
        "lib/event_engine/thread_pool/original_thread_pool.cc",
        "lib/event_engine/thread_pool/thread_pool_factory.cc",
        "lib/event_engine/thread_pool/work_stealing_thread_pool.cc",
    ],
    hdrs = [
        "lib/event_engine/thread_pool/original_thread_pool.h",
        "lib/event_engine/thread_pool/thread_pool.h",
        "lib/event_engine/thread_pool/work_stealing_thread_pool.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_set",
        "absl/functional:any_invocable",
        "absl/time",
    ],
    deps = [
        "common_event_engine_closures",
        "event_engine_basic_work_queue",
        "event_engine_thread_local",
        "event_engine_trace",
        "event_engine_work_queue",
        "experiments",
        "forkable",
        "notification",
        "time",
        "useful",
        "//:backoff",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_base_hdrs",
    srcs = [],
    hdrs = [
        "lib/event_engine/posix.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_timer_manager",
    srcs = ["lib/event_engine/posix_engine/timer_manager.cc"],
    hdrs = [
        "lib/event_engine/posix_engine/timer_manager.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/time",
        "absl/types:optional",
    ],
    deps = [
        "event_engine_thread_pool",
        "forkable",
        "notification",
        "posix_event_engine_timer",
        "time",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_event_poller",
    srcs = [],
    hdrs = [
        "lib/event_engine/posix_engine/event_poller.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "event_engine_poller",
        "posix_event_engine_closure",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_closure",
    srcs = [],
    hdrs = [
        "lib/event_engine/posix_engine/posix_engine_closure.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
    ],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_lockfree_event",
    srcs = [
        "lib/event_engine/posix_engine/lockfree_event.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/lockfree_event.h",
    ],
    external_deps = ["absl/status"],
    deps = [
        "gpr_atm",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "status_helper",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_wakeup_fd_posix",
    hdrs = [
        "lib/event_engine/posix_engine/wakeup_fd_posix.h",
    ],
    external_deps = ["absl/status"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "posix_event_engine_wakeup_fd_posix_pipe",
    srcs = [
        "lib/event_engine/posix_engine/wakeup_fd_pipe.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/wakeup_fd_pipe.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "iomgr_port",
        "posix_event_engine_wakeup_fd_posix",
        "strerror",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_wakeup_fd_posix_eventfd",
    srcs = [
        "lib/event_engine/posix_engine/wakeup_fd_eventfd.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/wakeup_fd_eventfd.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "iomgr_port",
        "posix_event_engine_wakeup_fd_posix",
        "strerror",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_wakeup_fd_posix_default",
    srcs = [
        "lib/event_engine/posix_engine/wakeup_fd_posix_default.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/wakeup_fd_posix_default.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "iomgr_port",
        "posix_event_engine_wakeup_fd_posix",
        "posix_event_engine_wakeup_fd_posix_eventfd",
        "posix_event_engine_wakeup_fd_posix_pipe",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_poller_posix_epoll1",
    srcs = [
        "lib/event_engine/posix_engine/ev_epoll1_linux.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/ev_epoll1_linux.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/functional:function_ref",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "event_engine_poller",
        "event_engine_time_util",
        "forkable",
        "iomgr_port",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "posix_event_engine_internal_errqueue",
        "posix_event_engine_lockfree_event",
        "posix_event_engine_wakeup_fd_posix",
        "posix_event_engine_wakeup_fd_posix_default",
        "status_helper",
        "strerror",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_poller_posix_poll",
    srcs = [
        "lib/event_engine/posix_engine/ev_poll_posix.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/ev_poll_posix.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/functional:any_invocable",
        "absl/functional:function_ref",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "common_event_engine_closures",
        "event_engine_poller",
        "event_engine_time_util",
        "iomgr_port",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "posix_event_engine_wakeup_fd_posix",
        "posix_event_engine_wakeup_fd_posix_default",
        "status_helper",
        "strerror",
        "time",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_poller_posix_default",
    srcs = [
        "lib/event_engine/posix_engine/event_poller_posix_default.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/event_poller_posix_default.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "iomgr_port",
        "posix_event_engine_event_poller",
        "posix_event_engine_poller_posix_epoll1",
        "posix_event_engine_poller_posix_poll",
        "//:config_vars",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_internal_errqueue",
    srcs = [
        "lib/event_engine/posix_engine/internal_errqueue.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/internal_errqueue.h",
    ],
    deps = [
        "iomgr_port",
        "strerror",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_traced_buffer_list",
    srcs = [
        "lib/event_engine/posix_engine/traced_buffer_list.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/traced_buffer_list.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/types:optional",
    ],
    deps = [
        "iomgr_port",
        "posix_event_engine_internal_errqueue",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_endpoint",
    srcs = [
        "lib/event_engine/posix_engine/posix_endpoint.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/posix_endpoint.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/hash",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "event_engine_common",
        "event_engine_tcp_socket_utils",
        "experiments",
        "iomgr_port",
        "load_file",
        "memory_quota",
        "posix_event_engine_base_hdrs",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "posix_event_engine_internal_errqueue",
        "posix_event_engine_tcp_socket_utils",
        "posix_event_engine_traced_buffer_list",
        "ref_counted",
        "resource_quota",
        "slice",
        "status_helper",
        "strerror",
        "time",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "event_engine_utils",
    srcs = ["lib/event_engine/utils.cc"],
    hdrs = ["lib/event_engine/utils.h"],
    external_deps = ["absl/strings"],
    deps = [
        "time",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_tcp_socket_utils",
    srcs = [
        "lib/event_engine/posix_engine/tcp_socket_utils.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/tcp_socket_utils.h",
    ],
    external_deps = [
        "absl/cleanup",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "event_engine_tcp_socket_utils",
        "iomgr_port",
        "resource_quota",
        "socket_mutator",
        "status_helper",
        "strerror",
        "time",
        "useful",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_listener_utils",
    srcs = [
        "lib/event_engine/posix_engine/posix_engine_listener_utils.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/posix_engine_listener_utils.h",
    ],
    external_deps = [
        "absl/cleanup",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "event_engine_tcp_socket_utils",
        "iomgr_port",
        "posix_event_engine_tcp_socket_utils",
        "socket_mutator",
        "status_helper",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_listener",
    srcs = [
        "lib/event_engine/posix_engine/posix_engine_listener.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/posix_engine_listener.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "event_engine_tcp_socket_utils",
        "iomgr_port",
        "posix_event_engine_base_hdrs",
        "posix_event_engine_closure",
        "posix_event_engine_endpoint",
        "posix_event_engine_event_poller",
        "posix_event_engine_listener_utils",
        "posix_event_engine_tcp_socket_utils",
        "socket_mutator",
        "status_helper",
        "time",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "posix_event_engine",
    srcs = ["lib/event_engine/posix_engine/posix_engine.cc"],
    hdrs = ["lib/event_engine/posix_engine/posix_engine.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/hash",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "ares_resolver",
        "event_engine_common",
        "event_engine_poller",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
        "event_engine_trace",
        "event_engine_utils",
        "init_internally",
        "iomgr_port",
        "posix_event_engine_base_hdrs",
        "posix_event_engine_closure",
        "posix_event_engine_endpoint",
        "posix_event_engine_event_poller",
        "posix_event_engine_listener",
        "posix_event_engine_poller_posix_default",
        "posix_event_engine_tcp_socket_utils",
        "posix_event_engine_timer",
        "posix_event_engine_timer_manager",
        "useful",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc_trace",
        "//:orphanable",
    ],
)

grpc_cc_library(
    name = "windows_event_engine",
    srcs = ["lib/event_engine/windows/windows_engine.cc"],
    hdrs = ["lib/event_engine/windows/windows_engine.h"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args_endpoint_config",
        "common_event_engine_closures",
        "error",
        "event_engine_common",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
        "event_engine_trace",
        "event_engine_utils",
        "init_internally",
        "posix_event_engine_timer_manager",
        "time",
        "windows_endpoint",
        "windows_event_engine_listener",
        "windows_iocp",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "windows_iocp",
    srcs = [
        "lib/event_engine/windows/iocp.cc",
        "lib/event_engine/windows/win_socket.cc",
    ],
    hdrs = [
        "lib/event_engine/windows/iocp.h",
        "lib/event_engine/windows/win_socket.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/strings:str_format",
    ],
    deps = [
        "error",
        "event_engine_poller",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
        "event_engine_time_util",
        "event_engine_trace",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "windows_endpoint",
    srcs = [
        "lib/event_engine/windows/windows_endpoint.cc",
    ],
    hdrs = [
        "lib/event_engine/windows/windows_endpoint.h",
    ],
    external_deps = [
        "absl/cleanup",
        "absl/functional:any_invocable",
        "absl/status",
        "absl/strings:str_format",
    ],
    deps = [
        "error",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
        "event_engine_trace",
        "status_helper",
        "windows_iocp",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "windows_event_engine_listener",
    srcs = [
        "lib/event_engine/windows/windows_listener.cc",
    ],
    hdrs = [
        "lib/event_engine/windows/windows_listener.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
    ],
    deps = [
        "common_event_engine_closures",
        "error",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
        "event_engine_trace",
        "windows_endpoint",
        "windows_iocp",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "cf_event_engine",
    srcs = [
        "lib/event_engine/cf_engine/cf_engine.cc",
        "lib/event_engine/cf_engine/cfstream_endpoint.cc",
    ],
    hdrs = [
        "lib/event_engine/cf_engine/cf_engine.h",
        "lib/event_engine/cf_engine/cfstream_endpoint.h",
        "lib/event_engine/cf_engine/cftype_unique_ref.h",
    ],
    external_deps = ["absl/strings:str_format"],
    deps = [
        "event_engine_common",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
        "event_engine_trace",
        "event_engine_utils",
        "init_internally",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "posix_event_engine_lockfree_event",
        "posix_event_engine_timer_manager",
        "ref_counted",
        "strerror",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "event_engine_tcp_socket_utils",
    srcs = [
        "lib/event_engine/tcp_socket_utils.cc",
    ],
    hdrs = [
        "lib/event_engine/tcp_socket_utils.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    deps = [
        "iomgr_port",
        "resolved_address",
        "status_helper",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:gpr_platform",
        "//:parse_address",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "event_engine_trace",
    srcs = [
        "lib/event_engine/trace.cc",
    ],
    hdrs = [
        "lib/event_engine/trace.h",
    ],
    deps = [
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "event_engine_shim",
    srcs = [
        "lib/event_engine/shim.cc",
    ],
    hdrs = [
        "lib/event_engine/shim.h",
    ],
    deps = [
        "experiments",
        "iomgr_port",
        "//:gpr_platform",
    ],
)

# NOTE: this target gets replaced inside Google's build system to be one that
# integrates with other internal systems better. Please do not rename or fold
# this into other targets.
grpc_cc_library(
    name = "default_event_engine_factory",
    srcs = ["lib/event_engine/default_event_engine_factory.cc"],
    hdrs = ["lib/event_engine/default_event_engine_factory.h"],
    external_deps = ["absl/memory"],
    select_deps = [{
        "//:windows": ["windows_event_engine"],
        "//:windows_msvc": ["windows_event_engine"],
        "//:windows_other": ["windows_event_engine"],
        "//:mac": [
            "posix_event_engine",
            "cf_event_engine",
        ],
        "//:mac_x86_64": [
            "posix_event_engine",
            "cf_event_engine",
        ],
        "//:mac_arm64": [
            "posix_event_engine",
            "cf_event_engine",
        ],
        "//:ios": ["cf_event_engine"],
        "//:tvos": ["cf_event_engine"],
        "//:watchos": ["cf_event_engine"],
        "//conditions:default": ["posix_event_engine"],
    }],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "channel_args_endpoint_config",
    srcs = [
        "//src/core:lib/event_engine/channel_args_endpoint_config.cc",
    ],
    hdrs = [
        "//src/core:lib/event_engine/channel_args_endpoint_config.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "channel_args",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "thready_event_engine",
    srcs = ["lib/event_engine/thready_event_engine/thready_event_engine.cc"],
    hdrs = ["lib/event_engine/thready_event_engine/thready_event_engine.h"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "default_event_engine",
    srcs = [
        "lib/event_engine/default_event_engine.cc",
    ],
    hdrs = [
        "lib/event_engine/default_event_engine.h",
    ],
    external_deps = ["absl/functional:any_invocable"],
    deps = [
        "channel_args",
        "context",
        "default_event_engine_factory",
        "event_engine_trace",
        "no_destruct",
        "thready_event_engine",
        "//:config",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "ares_resolver",
    srcs = [
        "lib/event_engine/ares_resolver.cc",
    ],
    hdrs = [
        "lib/event_engine/ares_resolver.h",
        "lib/event_engine/grpc_polled_fd.h",
        "lib/event_engine/posix_engine/grpc_polled_fd_posix.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/hash",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:variant",
        "cares",
    ],
    deps = [
        "error",
        "event_engine_time_util",
        "grpc_sockaddr",
        "iomgr_port",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "posix_event_engine_tcp_socket_utils",
        "resolved_address",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc_trace",
        "//:orphanable",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "channel_args_preconditioning",
    srcs = [
        "lib/channel/channel_args_preconditioning.cc",
    ],
    hdrs = [
        "lib/channel/channel_args_preconditioning.h",
    ],
    deps = [
        "channel_args",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "pid_controller",
    srcs = [
        "lib/transport/pid_controller.cc",
    ],
    hdrs = [
        "lib/transport/pid_controller.h",
    ],
    deps = [
        "useful",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "bdp_estimator",
    srcs = [
        "lib/transport/bdp_estimator.cc",
    ],
    hdrs = ["lib/transport/bdp_estimator.h"],
    external_deps = ["absl/strings"],
    deps = [
        "time",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "percent_encoding",
    srcs = [
        "lib/slice/percent_encoding.cc",
    ],
    hdrs = [
        "lib/slice/percent_encoding.h",
    ],
    deps = [
        "bitset",
        "slice",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "socket_mutator",
    srcs = [
        "lib/iomgr/socket_mutator.cc",
    ],
    hdrs = [
        "lib/iomgr/socket_mutator.h",
    ],
    visibility = ["@grpc:alt_grpc_base_legacy"],
    deps = [
        "channel_args",
        "useful",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "pollset_set",
    srcs = [
        "lib/iomgr/pollset_set.cc",
    ],
    hdrs = [
        "lib/iomgr/pollset_set.h",
    ],
    deps = [
        "iomgr_fwd",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "histogram_view",
    srcs = [
        "lib/debug/histogram_view.cc",
    ],
    hdrs = [
        "lib/debug/histogram_view.h",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "stats_data",
    srcs = [
        "lib/debug/stats_data.cc",
    ],
    hdrs = [
        "lib/debug/stats_data.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "histogram_view",
        "per_cpu",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "per_cpu",
    srcs = [
        "lib/gprpp/per_cpu.cc",
    ],
    hdrs = [
        "lib/gprpp/per_cpu.h",
    ],
    deps = [
        "useful",
        "//:exec_ctx",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "event_log",
    srcs = [
        "lib/debug/event_log.cc",
    ],
    hdrs = [
        "lib/debug/event_log.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "per_cpu",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "load_file",
    srcs = [
        "lib/gprpp/load_file.cc",
    ],
    hdrs = [
        "lib/gprpp/load_file.h",
    ],
    external_deps = [
        "absl/cleanup",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "slice",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "http2_errors",
    hdrs = [
        "lib/transport/http2_errors.h",
    ],
)

grpc_cc_library(
    name = "channel_stack_type",
    srcs = [
        "lib/surface/channel_stack_type.cc",
    ],
    hdrs = [
        "lib/surface/channel_stack_type.h",
    ],
    language = "c++",
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "channel_init",
    srcs = [
        "lib/surface/channel_init.cc",
    ],
    hdrs = [
        "lib/surface/channel_init.h",
    ],
    language = "c++",
    deps = [
        "channel_stack_type",
        "//:channel_stack_builder",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "single_set_ptr",
    hdrs = [
        "lib/gprpp/single_set_ptr.h",
    ],
    language = "c++",
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "grpc_service_config",
    hdrs = [
        "lib/service_config/service_config.h",
        "lib/service_config/service_config_call_data.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "arena",
        "chunked_vector",
        "ref_counted",
        "service_config_parser",
        "slice_refcount",
        "unique_type_name",
        "useful",
        "//:gpr_platform",
        "//:legacy_context",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "service_config_parser",
    srcs = [
        "lib/service_config/service_config_parser.cc",
    ],
    hdrs = [
        "lib/service_config/service_config_parser.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "channel_args",
        "json",
        "validation_errors",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "notification",
    hdrs = [
        "lib/gprpp/notification.h",
    ],
    external_deps = ["absl/time"],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "channel_args",
    srcs = [
        "lib/channel/channel_args.cc",
    ],
    hdrs = [
        "lib/channel/channel_args.h",
    ],
    external_deps = [
        "absl/meta:type_traits",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    visibility = [
        "@grpc:alt_grpc_base_legacy",
    ],
    deps = [
        "avl",
        "channel_stack_type",
        "dual_ref_counted",
        "match",
        "ref_counted",
        "time",
        "useful",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "resolved_address",
    hdrs = ["lib/iomgr/resolved_address.h"],
    language = "c++",
    deps = [
        "iomgr_port",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "lb_policy",
    srcs = ["lib/load_balancing/lb_policy.cc"],
    hdrs = ["lib/load_balancing/lb_policy.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
    ],
    deps = [
        "channel_args",
        "closure",
        "dual_ref_counted",
        "error",
        "grpc_backend_metric_data",
        "iomgr_fwd",
        "pollset_set",
        "ref_counted",
        "subchannel_interface",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "lb_policy_factory",
    hdrs = ["lib/load_balancing/lb_policy_factory.h"],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "json",
        "lb_policy",
        "//:gpr_platform",
        "//:orphanable",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "lb_policy_registry",
    srcs = ["lib/load_balancing/lb_policy_registry.cc"],
    hdrs = ["lib/load_balancing/lb_policy_registry.h"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "json",
        "lb_policy",
        "lb_policy_factory",
        "//:gpr",
        "//:orphanable",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "subchannel_interface",
    hdrs = ["lib/load_balancing/subchannel_interface.h"],
    external_deps = ["absl/status"],
    deps = [
        "dual_ref_counted",
        "iomgr_fwd",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "delegating_helper",
    hdrs = ["lib/load_balancing/delegating_helper.h"],
    external_deps = [
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "lb_policy",
        "subchannel_interface",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:grpc_security_base",
        "//:ref_counted_ptr",
        "//:server_address",
    ],
)

grpc_cc_library(
    name = "proxy_mapper",
    hdrs = ["lib/handshaker/proxy_mapper.h"],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "channel_args",
        "resolved_address",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "proxy_mapper_registry",
    srcs = ["lib/handshaker/proxy_mapper_registry.cc"],
    hdrs = ["lib/handshaker/proxy_mapper_registry.h"],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "channel_args",
        "proxy_mapper",
        "resolved_address",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "grpc_server_config_selector",
    hdrs = [
        "ext/filters/server_config_selector/server_config_selector.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "dual_ref_counted",
        "grpc_service_config",
        "ref_counted",
        "service_config_parser",
        "useful",
        "//:gpr_platform",
        "//:grpc_base",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_server_config_selector_filter",
    srcs = [
        "ext/filters/server_config_selector/server_config_selector_filter.cc",
    ],
    hdrs = [
        "ext/filters/server_config_selector/server_config_selector_filter.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "context",
        "grpc_server_config_selector",
        "grpc_service_config",
        "status_helper",
        "//:gpr",
        "//:grpc_base",
        "//:legacy_context",
        "//:promise",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "sorted_pack",
    hdrs = [
        "lib/gprpp/sorted_pack.h",
    ],
    language = "c++",
    deps = [
        "type_list",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "type_list",
    hdrs = [
        "lib/gprpp/type_list.h",
    ],
    language = "c++",
)

grpc_cc_library(
    name = "if_list",
    hdrs = [
        "lib/gprpp/if_list.h",
    ],
    language = "c++",
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "certificate_provider_factory",
    hdrs = [
        "lib/security/certificate_provider/certificate_provider_factory.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "json",
        "json_args",
        "ref_counted",
        "validation_errors",
        "//:alts_util",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "certificate_provider_registry",
    srcs = [
        "lib/security/certificate_provider/certificate_provider_registry.cc",
    ],
    hdrs = [
        "lib/security/certificate_provider/certificate_provider_registry.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "certificate_provider_factory",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "grpc_audit_logging",
    srcs = [
        "lib/security/authorization/audit_logging.cc",
        "lib/security/authorization/stdout_logger.cc",
    ],
    hdrs = [
        "lib/security/authorization/audit_logging.h",
        "lib/security/authorization/stdout_logger.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
    ],
    deps = [
        "//:gpr",
        "//:grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_authorization_base",
    srcs = [
        "lib/security/authorization/authorization_policy_provider_vtable.cc",
        "lib/security/authorization/evaluate_args.cc",
        "lib/security/authorization/grpc_server_authz_filter.cc",
    ],
    hdrs = [
        "lib/security/authorization/authorization_engine.h",
        "lib/security/authorization/authorization_policy_provider.h",
        "lib/security/authorization/evaluate_args.h",
        "lib/security/authorization/grpc_server_authz_filter.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "dual_ref_counted",
        "ref_counted",
        "resolved_address",
        "slice",
        "useful",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_credentials_util",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:parse_address",
        "//:promise",
        "//:ref_counted_ptr",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_fake_credentials",
    srcs = [
        "lib/security/credentials/fake/fake_credentials.cc",
        "lib/security/security_connector/fake/fake_security_connector.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "lib/security/credentials/fake/fake_credentials.h",
        "lib/security/security_connector/fake/fake_security_connector.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "slice",
        "unique_type_name",
        "useful",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:handshaker",
        "//:promise",
        "//:ref_counted_ptr",
        "//:tsi_base",
        "//:tsi_fake_credentials",
    ],
)

grpc_cc_library(
    name = "grpc_insecure_credentials",
    srcs = [
        "lib/security/credentials/insecure/insecure_credentials.cc",
        "lib/security/security_connector/insecure/insecure_security_connector.cc",
    ],
    hdrs = [
        "lib/security/credentials/insecure/insecure_credentials.h",
        "lib/security/security_connector/insecure/insecure_security_connector.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "tsi_local_credentials",
        "unique_type_name",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:handshaker",
        "//:promise",
        "//:ref_counted_ptr",
        "//:tsi_base",
    ],
)

grpc_cc_library(
    name = "tsi_local_credentials",
    srcs = [
        "tsi/local_transport_security.cc",
    ],
    hdrs = [
        "tsi/local_transport_security.h",
    ],
    language = "c++",
    deps = [
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:tsi_base",
    ],
)

grpc_cc_library(
    name = "grpc_local_credentials",
    srcs = [
        "lib/security/credentials/local/local_credentials.cc",
        "lib/security/security_connector/local/local_security_connector.cc",
    ],
    hdrs = [
        "lib/security/credentials/local/local_credentials.h",
        "lib/security/security_connector/local/local_security_connector.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "grpc_sockaddr",
        "iomgr_fwd",
        "resolved_address",
        "tsi_local_credentials",
        "unique_type_name",
        "useful",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_security_base",
        "//:handshaker",
        "//:parse_address",
        "//:promise",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:tsi_base",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_ssl_credentials",
    srcs = [
        "lib/security/credentials/ssl/ssl_credentials.cc",
        "lib/security/security_connector/ssl/ssl_security_connector.cc",
    ],
    hdrs = [
        "lib/security/credentials/ssl/ssl_credentials.h",
        "lib/security/security_connector/ssl/ssl_security_connector.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "unique_type_name",
        "useful",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:handshaker",
        "//:promise",
        "//:ref_counted_ptr",
        "//:tsi_base",
        "//:tsi_ssl_credentials",
        "//:tsi_ssl_session_cache",
    ],
)

grpc_cc_library(
    name = "grpc_google_default_credentials",
    srcs = [
        "lib/security/credentials/google_default/credentials_generic.cc",
        "lib/security/credentials/google_default/google_default_credentials.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "lib/security/credentials/google_default/google_default_credentials.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    deps = [
        "channel_args",
        "closure",
        "env",
        "error",
        "grpc_external_account_credentials",
        "grpc_lb_xds_channel_args",
        "grpc_oauth2_credentials",
        "grpc_ssl_credentials",
        "iomgr_fwd",
        "json",
        "json_reader",
        "slice",
        "slice_refcount",
        "status_helper",
        "time",
        "unique_type_name",
        "useful",
        "//:alts_util",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_alts_credentials",
        "//:grpc_base",
        "//:grpc_jwt_credentials",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:httpcli",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "strerror",
    srcs = [
        "lib/gprpp/strerror.cc",
    ],
    hdrs = [
        "lib/gprpp/strerror.h",
    ],
    external_deps = ["absl/strings:str_format"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_tls_credentials",
    srcs = [
        "lib/security/credentials/tls/grpc_tls_certificate_distributor.cc",
        "lib/security/credentials/tls/grpc_tls_certificate_provider.cc",
        "lib/security/credentials/tls/grpc_tls_certificate_verifier.cc",
        "lib/security/credentials/tls/grpc_tls_credentials_options.cc",
        "lib/security/credentials/tls/tls_credentials.cc",
        "lib/security/security_connector/tls/tls_security_connector.cc",
    ],
    hdrs = [
        "lib/security/credentials/tls/grpc_tls_certificate_distributor.h",
        "lib/security/credentials/tls/grpc_tls_certificate_provider.h",
        "lib/security/credentials/tls/grpc_tls_certificate_verifier.h",
        "lib/security/credentials/tls/grpc_tls_credentials_options.h",
        "lib/security/credentials/tls/tls_credentials.h",
        "lib/security/security_connector/tls/tls_security_connector.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/functional:bind_front",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "libcrypto",
        "libssl",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "ref_counted",
        "slice",
        "slice_refcount",
        "status_helper",
        "unique_type_name",
        "useful",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_credentials_util",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:handshaker",
        "//:promise",
        "//:ref_counted_ptr",
        "//:tsi_base",
        "//:tsi_ssl_credentials",
        "//:tsi_ssl_session_cache",
    ],
)

grpc_cc_library(
    name = "grpc_iam_credentials",
    srcs = [
        "lib/security/credentials/iam/iam_credentials.cc",
    ],
    hdrs = [
        "lib/security/credentials/iam/iam_credentials.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "slice",
        "unique_type_name",
        "useful",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:promise",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_oauth2_credentials",
    srcs = [
        "lib/security/credentials/oauth2/oauth2_credentials.cc",
    ],
    hdrs = [
        "lib/security/credentials/oauth2/oauth2_credentials.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "activity",
        "arena_promise",
        "closure",
        "context",
        "error",
        "httpcli_ssl_credentials",
        "json",
        "json_reader",
        "poll",
        "pollset_set",
        "ref_counted",
        "slice",
        "slice_refcount",
        "status_helper",
        "time",
        "unique_type_name",
        "useful",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_credentials_util",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:httpcli",
        "//:orphanable",
        "//:promise",
        "//:ref_counted_ptr",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_external_account_credentials",
    srcs = [
        "lib/security/credentials/external/aws_external_account_credentials.cc",
        "lib/security/credentials/external/aws_request_signer.cc",
        "lib/security/credentials/external/external_account_credentials.cc",
        "lib/security/credentials/external/file_external_account_credentials.cc",
        "lib/security/credentials/external/url_external_account_credentials.cc",
    ],
    hdrs = [
        "lib/security/credentials/external/aws_external_account_credentials.h",
        "lib/security/credentials/external/aws_request_signer.h",
        "lib/security/credentials/external/external_account_credentials.h",
        "lib/security/credentials/external/file_external_account_credentials.h",
        "lib/security/credentials/external/url_external_account_credentials.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "absl/types:optional",
        "libcrypto",
    ],
    language = "c++",
    deps = [
        "closure",
        "env",
        "error",
        "grpc_oauth2_credentials",
        "httpcli_ssl_credentials",
        "json",
        "json_reader",
        "json_writer",
        "slice",
        "slice_refcount",
        "status_helper",
        "time",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_credentials_util",
        "//:grpc_security_base",
        "//:httpcli",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "httpcli_ssl_credentials",
    srcs = [
        "lib/http/httpcli_security_connector.cc",
    ],
    hdrs = [
        "lib/http/httpcli_ssl_credentials.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "unique_type_name",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:handshaker",
        "//:promise",
        "//:ref_counted_ptr",
        "//:tsi_base",
        "//:tsi_ssl_credentials",
    ],
)

grpc_cc_library(
    name = "tsi_ssl_types",
    hdrs = [
        "tsi/ssl_types.h",
    ],
    external_deps = ["libssl"],
    language = "c++",
    deps = ["//:gpr_platform"],
)

# This target depends on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_matchers",
    srcs = [
        "lib/matchers/matchers.cc",
    ],
    hdrs = [
        "lib/matchers/matchers.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "re2",
    ],
    language = "c++",
    deps = ["//:gpr"],
)

# This target pulls in a dependency on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_rbac_engine",
    srcs = [
        "lib/security/authorization/grpc_authorization_engine.cc",
        "lib/security/authorization/matchers.cc",
        "lib/security/authorization/rbac_policy.cc",
    ],
    hdrs = [
        "lib/security/authorization/grpc_authorization_engine.h",
        "lib/security/authorization/matchers.h",
        "lib/security/authorization/rbac_policy.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "grpc_audit_logging",
        "grpc_authorization_base",
        "grpc_matchers",
        "resolved_address",
        "//:gpr",
        "//:grpc_base",
        "//:parse_address",
        "//:sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "json",
    hdrs = [
        "lib/json/json.h",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "json_reader",
    srcs = [
        "lib/json/json_reader.cc",
    ],
    hdrs = [
        "lib/json/json_reader.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:variant",
    ],
    visibility = ["@grpc:json_reader_legacy"],
    deps = [
        "json",
        "match",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "json_writer",
    srcs = [
        "lib/json/json_writer.cc",
    ],
    hdrs = [
        "lib/json/json_writer.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "json",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "json_util",
    srcs = ["lib/json/json_util.cc"],
    hdrs = ["lib/json/json_util.h"],
    external_deps = ["absl/strings"],
    deps = [
        "error",
        "json",
        "json_args",
        "json_object_loader",
        "no_destruct",
        "time",
        "validation_errors",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "json_args",
    hdrs = ["lib/json/json_args.h"],
    external_deps = ["absl/strings"],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "json_object_loader",
    srcs = ["lib/json/json_object_loader.cc"],
    hdrs = ["lib/json/json_object_loader.h"],
    external_deps = [
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "json",
        "json_args",
        "no_destruct",
        "time",
        "validation_errors",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "json_channel_args",
    hdrs = ["lib/json/json_channel_args.h"],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "channel_args",
        "json_args",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "idle_filter_state",
    srcs = [
        "ext/filters/channel_idle/idle_filter_state.cc",
    ],
    hdrs = [
        "ext/filters/channel_idle/idle_filter_state.h",
    ],
    language = "c++",
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_channel_idle_filter",
    srcs = [
        "ext/filters/channel_idle/channel_idle_filter.cc",
    ],
    hdrs = [
        "ext/filters/channel_idle/channel_idle_filter.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/types:optional",
    ],
    deps = [
        "activity",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_init",
        "channel_stack_type",
        "closure",
        "error",
        "exec_ctx_wakeup_scheduler",
        "http2_errors",
        "idle_filter_state",
        "loop",
        "poll",
        "single_set_ptr",
        "sleep",
        "status_helper",
        "time",
        "try_seq",
        "//:channel_stack_builder",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:promise",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_deadline_filter",
    srcs = [
        "ext/filters/deadline/deadline_filter.cc",
    ],
    hdrs = [
        "ext/filters/deadline/deadline_filter.h",
    ],
    external_deps = [
        "absl/status",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_init",
        "channel_stack_type",
        "closure",
        "context",
        "error",
        "status_helper",
        "time",
        "//:channel_stack_builder",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:iomgr_timer",
    ],
)

grpc_cc_library(
    name = "grpc_client_authority_filter",
    srcs = [
        "ext/filters/http/client_authority_filter.cc",
    ],
    hdrs = [
        "ext/filters/http/client_authority_filter.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "slice",
        "//:channel_stack_builder",
        "//:config",
        "//:gpr_platform",
        "//:grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_message_size_filter",
    srcs = [
        "ext/filters/message_size/message_size_filter.cc",
    ],
    hdrs = [
        "ext/filters/message_size/message_size_filter.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "activity",
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_init",
        "channel_stack_type",
        "context",
        "grpc_service_config",
        "json",
        "json_args",
        "json_object_loader",
        "latch",
        "poll",
        "race",
        "service_config_parser",
        "slice",
        "slice_buffer",
        "validation_errors",
        "//:channel_stack_builder",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
        "//:legacy_context",
    ],
)

grpc_cc_library(
    name = "grpc_fault_injection_filter",
    srcs = [
        "ext/filters/fault_injection/fault_injection_filter.cc",
        "ext/filters/fault_injection/fault_injection_service_config_parser.cc",
    ],
    hdrs = [
        "ext/filters/fault_injection/fault_injection_filter.h",
        "ext/filters/fault_injection/fault_injection_service_config_parser.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "context",
        "grpc_service_config",
        "json",
        "json_args",
        "json_object_loader",
        "service_config_parser",
        "sleep",
        "time",
        "try_seq",
        "validation_errors",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
        "//:legacy_context",
    ],
)

grpc_cc_library(
    name = "grpc_rbac_filter",
    srcs = [
        "ext/filters/rbac/rbac_filter.cc",
        "ext/filters/rbac/rbac_service_config_parser.cc",
    ],
    hdrs = [
        "ext/filters/rbac/rbac_filter.h",
        "ext/filters/rbac/rbac_service_config_parser.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "context",
        "error",
        "grpc_audit_logging",
        "grpc_authorization_base",
        "grpc_matchers",
        "grpc_rbac_engine",
        "grpc_service_config",
        "json",
        "json_args",
        "json_object_loader",
        "service_config_parser",
        "transport_fwd",
        "validation_errors",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:legacy_context",
        "//:promise",
    ],
)

grpc_cc_library(
    name = "grpc_stateful_session_filter",
    srcs = [
        "ext/filters/stateful_session/stateful_session_filter.cc",
        "ext/filters/stateful_session/stateful_session_service_config_parser.cc",
    ],
    hdrs = [
        "ext/filters/stateful_session/stateful_session_filter.h",
        "ext/filters/stateful_session/stateful_session_service_config_parser.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "context",
        "grpc_resolver_xds_header",
        "grpc_service_config",
        "json",
        "json_args",
        "json_object_loader",
        "map",
        "pipe",
        "poll",
        "service_config_parser",
        "slice",
        "time",
        "unique_type_name",
        "validation_errors",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:legacy_context",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_grpclb",
    srcs = [
        "ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.cc",
        "ext/filters/client_channel/lb_policy/grpclb/grpclb.cc",
        "ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.cc",
        "ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/lb_policy/grpclb/client_load_reporting_filter.h",
        "ext/filters/client_channel/lb_policy/grpclb/grpclb.h",
        "ext/filters/client_channel/lb_policy/grpclb/grpclb_client_stats.h",
        "ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:variant",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_init",
        "channel_stack_type",
        "closure",
        "context",
        "delegating_helper",
        "error",
        "gpr_atm",
        "grpc_sockaddr",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "map",
        "pipe",
        "poll",
        "pollset_set",
        "ref_counted",
        "resolved_address",
        "slice",
        "slice_refcount",
        "status_helper",
        "subchannel_interface",
        "time",
        "useful",
        "validation_errors",
        "//:backoff",
        "//:channel_stack_builder",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_grpclb_balancer_addresses",
        "//:grpc_lb_upb",
        "//:grpc_public_hdrs",
        "//:grpc_resolver",
        "//:grpc_resolver_fake",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:protobuf_duration_upb",
        "//:protobuf_timestamp_upb",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:sockaddr_utils",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "random_early_detection",
    srcs = [
        "lib/backoff/random_early_detection.cc",
    ],
    hdrs = [
        "lib/backoff/random_early_detection.h",
    ],
    external_deps = ["absl/random"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_backend_metric_data",
    hdrs = [
        "ext/filters/client_channel/lb_policy/backend_metric_data.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_backend_metric_provider",
    hdrs = [
        "ext/filters/backend_metrics/backend_metric_provider.h",
    ],
    language = "c++",
)

grpc_cc_library(
    name = "grpc_lb_policy_rls",
    srcs = [
        "ext/filters/client_channel/lb_policy/rls/rls.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/hash",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "closure",
        "delegating_helper",
        "dual_ref_counted",
        "error",
        "grpc_fake_credentials",
        "json",
        "json_args",
        "json_object_loader",
        "json_writer",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "slice",
        "slice_refcount",
        "status_helper",
        "time",
        "validation_errors",
        "//:backoff",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_public_hdrs",
        "//:grpc_resolver",
        "//:grpc_security_base",
        "//:grpc_service_config_impl",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:rls_upb",
        "//:server_address",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "upb_utils",
    hdrs = [
        "ext/xds/upb_utils.h",
    ],
    external_deps = [
        "absl/strings",
        "upb_lib",
    ],
    language = "c++",
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_xds_client",
    srcs = [
        "ext/xds/certificate_provider_store.cc",
        "ext/xds/file_watcher_certificate_provider_factory.cc",
        "ext/xds/xds_audit_logger_registry.cc",
        "ext/xds/xds_bootstrap_grpc.cc",
        "ext/xds/xds_certificate_provider.cc",
        "ext/xds/xds_client_grpc.cc",
        "ext/xds/xds_cluster.cc",
        "ext/xds/xds_cluster_specifier_plugin.cc",
        "ext/xds/xds_common_types.cc",
        "ext/xds/xds_endpoint.cc",
        "ext/xds/xds_health_status.cc",
        "ext/xds/xds_http_fault_filter.cc",
        "ext/xds/xds_http_filters.cc",
        "ext/xds/xds_http_rbac_filter.cc",
        "ext/xds/xds_http_stateful_session_filter.cc",
        "ext/xds/xds_lb_policy_registry.cc",
        "ext/xds/xds_listener.cc",
        "ext/xds/xds_route_config.cc",
        "ext/xds/xds_routing.cc",
        "ext/xds/xds_transport_grpc.cc",
        "lib/security/credentials/xds/xds_credentials.cc",
    ],
    hdrs = [
        "ext/xds/certificate_provider_store.h",
        "ext/xds/file_watcher_certificate_provider_factory.h",
        "ext/xds/xds_audit_logger_registry.h",
        "ext/xds/xds_bootstrap_grpc.h",
        "ext/xds/xds_certificate_provider.h",
        "ext/xds/xds_client_grpc.h",
        "ext/xds/xds_cluster.h",
        "ext/xds/xds_cluster_specifier_plugin.h",
        "ext/xds/xds_common_types.h",
        "ext/xds/xds_endpoint.h",
        "ext/xds/xds_health_status.h",
        "ext/xds/xds_http_fault_filter.h",
        "ext/xds/xds_http_filters.h",
        "ext/xds/xds_http_rbac_filter.h",
        "ext/xds/xds_http_stateful_session_filter.h",
        "ext/xds/xds_lb_policy_registry.h",
        "ext/xds/xds_listener.h",
        "ext/xds/xds_route_config.h",
        "ext/xds/xds_routing.h",
        "ext/xds/xds_transport_grpc.h",
        "lib/security/credentials/xds/xds_credentials.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:bind_front",
        "absl/memory",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/synchronization",
        "absl/types:optional",
        "absl/types:span",
        "absl/types:variant",
        "upb_lib",
        "upb_textformat_lib",
        "upb_json_lib",
        "re2",
        "upb_reflection",
        "upb_collections_lib",
    ],
    language = "c++",
    tags = ["nofixdeps"],
    deps = [
        "certificate_provider_factory",
        "certificate_provider_registry",
        "channel_args",
        "channel_creds_registry",
        "channel_fwd",
        "closure",
        "default_event_engine",
        "env",
        "envoy_admin_upb",
        "envoy_config_cluster_upb",
        "envoy_config_cluster_upbdefs",
        "envoy_config_core_upb",
        "envoy_config_endpoint_upb",
        "envoy_config_endpoint_upbdefs",
        "envoy_config_listener_upb",
        "envoy_config_listener_upbdefs",
        "envoy_config_rbac_upb",
        "envoy_config_route_upb",
        "envoy_config_route_upbdefs",
        "envoy_extensions_clusters_aggregate_upb",
        "envoy_extensions_clusters_aggregate_upbdefs",
        "envoy_extensions_filters_common_fault_upb",
        "envoy_extensions_filters_http_fault_upb",
        "envoy_extensions_filters_http_fault_upbdefs",
        "envoy_extensions_filters_http_rbac_upb",
        "envoy_extensions_filters_http_rbac_upbdefs",
        "envoy_extensions_filters_http_router_upb",
        "envoy_extensions_filters_http_router_upbdefs",
        "envoy_extensions_filters_http_stateful_session_upb",
        "envoy_extensions_filters_http_stateful_session_upbdefs",
        "envoy_extensions_filters_network_http_connection_manager_upb",
        "envoy_extensions_filters_network_http_connection_manager_upbdefs",
        "envoy_extensions_http_stateful_session_cookie_upb",
        "envoy_extensions_http_stateful_session_cookie_upbdefs",
        "envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_upb",
        "envoy_extensions_load_balancing_policies_pick_first_upb",
        "envoy_extensions_load_balancing_policies_ring_hash_upb",
        "envoy_extensions_load_balancing_policies_wrr_locality_upb",
        "envoy_extensions_transport_sockets_tls_upb",
        "envoy_extensions_transport_sockets_tls_upbdefs",
        "envoy_service_discovery_upb",
        "envoy_service_discovery_upbdefs",
        "envoy_service_load_stats_upb",
        "envoy_service_load_stats_upbdefs",
        "envoy_service_status_upb",
        "envoy_service_status_upbdefs",
        "envoy_type_http_upb",
        "envoy_type_matcher_upb",
        "envoy_type_upb",
        "error",
        "google_rpc_status_upb",
        "grpc_audit_logging",
        "grpc_fake_credentials",
        "grpc_fault_injection_filter",
        "grpc_lb_policy_pick_first",
        "grpc_lb_xds_channel_args",
        "grpc_matchers",
        "grpc_outlier_detection_header",
        "grpc_rbac_filter",
        "grpc_sockaddr",
        "grpc_stateful_session_filter",
        "grpc_tls_credentials",
        "grpc_transport_chttp2_client_connector",
        "init_internally",
        "iomgr_fwd",
        "json",
        "json_args",
        "json_object_loader",
        "json_reader",
        "json_util",
        "json_writer",
        "lb_policy_registry",
        "match",
        "pollset_set",
        "protobuf_any_upb",
        "protobuf_duration_upb",
        "protobuf_struct_upb",
        "protobuf_struct_upbdefs",
        "protobuf_timestamp_upb",
        "protobuf_wrappers_upb",
        "ref_counted",
        "resolved_address",
        "rls_config_upb",
        "rls_config_upbdefs",
        "slice",
        "slice_refcount",
        "status_helper",
        "time",
        "unique_type_name",
        "upb_utils",
        "useful",
        "validation_errors",
        "xds_type_upb",
        "xds_type_upbdefs",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_credentials_util",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:iomgr_timer",
        "//:orphanable",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:sockaddr_utils",
        "//:tsi_ssl_credentials",
        "//:uri_parser",
        "//:work_serializer",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_xds_channel_stack_modifier",
    srcs = [
        "ext/xds/xds_channel_stack_modifier.cc",
    ],
    hdrs = [
        "ext/xds/xds_channel_stack_modifier.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "ref_counted",
        "useful",
        "//:channel_stack_builder",
        "//:config",
        "//:gpr_platform",
        "//:grpc_base",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_xds_server_config_fetcher",
    srcs = [
        "ext/xds/xds_server_config_fetcher.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "channel_args_preconditioning",
        "channel_fwd",
        "grpc_server_config_selector",
        "grpc_server_config_selector_filter",
        "grpc_service_config",
        "grpc_sockaddr",
        "grpc_tls_credentials",
        "grpc_xds_channel_stack_modifier",
        "grpc_xds_client",
        "iomgr_fwd",
        "match",
        "resolved_address",
        "slice_refcount",
        "unique_type_name",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_service_config_impl",
        "//:grpc_trace",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "channel_creds_registry_init",
    srcs = [
        "lib/security/credentials/channel_creds_registry_init.cc",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "channel_creds_registry",
        "grpc_fake_credentials",
        "grpc_google_default_credentials",
        "grpc_tls_credentials",
        "json",
        "json_args",
        "json_object_loader",
        "time",
        "validation_errors",
        "//:config",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_security_base",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_cds",
    srcs = [
        "ext/filters/client_channel/lb_policy/xds/cds.cc",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "delegating_helper",
        "grpc_matchers",
        "grpc_outlier_detection_header",
        "grpc_tls_credentials",
        "grpc_xds_client",
        "json",
        "json_args",
        "json_object_loader",
        "json_writer",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "match",
        "pollset_set",
        "time",
        "unique_type_name",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_xds_channel_args",
    hdrs = [
        "ext/filters/client_channel/lb_policy/xds/xds_channel_args.h",
    ],
    language = "c++",
    deps = [
        "//:gpr_platform",
        "//:server_address",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_resolver",
    srcs = [
        "ext/filters/client_channel/lb_policy/xds/xds_cluster_resolver.cc",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "delegating_helper",
        "grpc_lb_address_filtering",
        "grpc_lb_xds_channel_args",
        "grpc_xds_client",
        "json",
        "json_args",
        "json_object_loader",
        "json_writer",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_resolver",
        "//:grpc_resolver_fake",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:work_serializer",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_impl",
    srcs = [
        "ext/filters/client_channel/lb_policy/xds/xds_cluster_impl.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "delegating_helper",
        "grpc_backend_metric_data",
        "grpc_lb_xds_channel_args",
        "grpc_xds_client",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "ref_counted",
        "subchannel_interface",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_manager",
    srcs = [
        "ext/filters/client_channel/lb_policy/xds/xds_cluster_manager.cc",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "delegating_helper",
        "grpc_resolver_xds_header",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "time",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_wrr_locality",
    srcs = [
        "ext/filters/client_channel/lb_policy/xds/xds_wrr_locality.cc",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "delegating_helper",
        "grpc_lb_xds_channel_args",
        "json",
        "json_args",
        "json_object_loader",
        "json_writer",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_address_filtering",
    srcs = [
        "ext/filters/client_channel/lb_policy/address_filtering.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/lb_policy/address_filtering.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "ref_counted",
        "//:gpr_platform",
        "//:ref_counted_ptr",
        "//:server_address",
    ],
)

grpc_cc_library(
    name = "health_check_client",
    srcs = [
        "ext/filters/client_channel/lb_policy/health_check_client.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/lb_policy/health_check_client.h",
        "ext/filters/client_channel/lb_policy/health_check_client_internal.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "pollset_set",
        "slice",
        "subchannel_interface",
        "unique_type_name",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_health_upb",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_subchannel_list",
    hdrs = [
        "ext/filters/client_channel/lb_policy/subchannel_list.h",
    ],
    external_deps = [
        "absl/status",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "dual_ref_counted",
        "gpr_manual_constructor",
        "health_check_client",
        "iomgr_fwd",
        "lb_policy",
        "subchannel_interface",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_pick_first",
    srcs = [
        "ext/filters/client_channel/lb_policy/pick_first/pick_first.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/lb_policy/pick_first/pick_first.h",
    ],
    external_deps = [
        "absl/algorithm:container",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "env",
        "grpc_lb_subchannel_list",
        "grpc_outlier_detection_header",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "subchannel_interface",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_ring_hash",
    srcs = [
        "ext/filters/client_channel/lb_policy/ring_hash/ring_hash.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/lb_policy/ring_hash/ring_hash.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "xxhash",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "closure",
        "error",
        "grpc_lb_subchannel_list",
        "grpc_service_config",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "ref_counted",
        "subchannel_interface",
        "unique_type_name",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:sockaddr_utils",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_round_robin",
    srcs = [
        "ext/filters/client_channel/lb_policy/round_robin/round_robin.cc",
    ],
    external_deps = [
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "grpc_lb_subchannel_list",
        "json",
        "lb_policy",
        "lb_policy_factory",
        "subchannel_interface",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "static_stride_scheduler",
    srcs = [
        "ext/filters/client_channel/lb_policy/weighted_round_robin/static_stride_scheduler.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/lb_policy/weighted_round_robin/static_stride_scheduler.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/types:optional",
        "absl/types:span",
    ],
    language = "c++",
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "grpc_lb_policy_weighted_round_robin",
    srcs = [
        "ext/filters/client_channel/lb_policy/weighted_round_robin/weighted_round_robin.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "grpc_backend_metric_data",
        "grpc_lb_subchannel_list",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "ref_counted",
        "resolved_address",
        "static_stride_scheduler",
        "subchannel_interface",
        "time",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:sockaddr_utils",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_outlier_detection_header",
    hdrs = [
        "ext/filters/client_channel/lb_policy/outlier_detection/outlier_detection.h",
    ],
    external_deps = ["absl/types:optional"],
    language = "c++",
    deps = [
        "json",
        "json_args",
        "json_object_loader",
        "time",
        "validation_errors",
        "//:gpr_platform",
        "//:server_address",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_outlier_detection",
    srcs = [
        "ext/filters/client_channel/lb_policy/outlier_detection/outlier_detection.cc",
    ],
    external_deps = [
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "delegating_helper",
        "grpc_outlier_detection_header",
        "health_check_client",
        "iomgr_fwd",
        "json",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "match",
        "pollset_set",
        "ref_counted",
        "subchannel_interface",
        "unique_type_name",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:sockaddr_utils",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_priority",
    srcs = [
        "ext/filters/client_channel/lb_policy/priority/priority.cc",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "delegating_helper",
        "grpc_lb_address_filtering",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "time",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_weighted_target",
    srcs = [
        "ext/filters/client_channel/lb_policy/weighted_target/weighted_target.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "delegating_helper",
        "grpc_lb_address_filtering",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "time",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_override_host",
    srcs = [
        "ext/filters/client_channel/lb_policy/xds/xds_override_host.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/lb_policy/xds/xds_override_host.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "closure",
        "delegating_helper",
        "error",
        "grpc_stateful_session_filter",
        "grpc_xds_client",
        "iomgr_fwd",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "match",
        "pollset_set",
        "subchannel_interface",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:sockaddr_utils",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "lb_server_load_reporting_filter",
    srcs = [
        "ext/filters/load_reporting/server_load_reporting_filter.cc",
    ],
    hdrs = [
        "ext/filters/load_reporting/registered_opencensus_objects.h",
        "ext/filters/load_reporting/server_load_reporting_filter.h",
        "//:src/cpp/server/load_reporter/constants.h",
    ],
    external_deps = [
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "opencensus-stats",
        "opencensus-tags",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "context",
        "grpc_sockaddr",
        "resolved_address",
        "seq",
        "slice",
        "//:channel_stack_builder",
        "//:config",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:parse_address",
        "//:promise",
        "//:uri_parser",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "grpc_backend_metric_filter",
    srcs = [
        "ext/filters/backend_metrics/backend_metric_filter.cc",
    ],
    hdrs = [
        "ext/filters/backend_metrics/backend_metric_filter.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
        "upb_lib",
    ],
    language = "c++",
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "context",
        "grpc_backend_metric_data",
        "grpc_backend_metric_provider",
        "map",
        "slice",
        "//:channel_stack_builder",
        "//:config",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_trace",
        "//:legacy_context",
        "//:xds_orca_upb",
    ],
)

grpc_cc_library(
    name = "polling_resolver",
    srcs = [
        "ext/filters/client_channel/resolver/polling_resolver.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/resolver/polling_resolver.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "grpc_service_config",
        "iomgr_fwd",
        "time",
        "//:backoff",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_resolver",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri_parser",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "service_config_helper",
    srcs = ["ext/filters/client_channel/resolver/dns/event_engine/service_config_helper.cc"],
    hdrs = ["ext/filters/client_channel/resolver/dns/event_engine/service_config_helper.h"],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "json",
        "json_args",
        "json_object_loader",
        "json_reader",
        "json_writer",
        "status_helper",
        "//:gpr_platform",
        "//:grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_event_engine",
    srcs = ["ext/filters/client_channel/resolver/dns/event_engine/event_engine_client_channel_resolver.cc"],
    hdrs = ["ext/filters/client_channel/resolver/dns/event_engine/event_engine_client_channel_resolver.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "event_engine_common",
        "grpc_service_config",
        "polling_resolver",
        "service_config_helper",
        "time",
        "validation_errors",
        "//:backoff",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_grpclb_balancer_addresses",
        "//:grpc_resolver",
        "//:grpc_service_config_impl",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_plugin",
    srcs = [
        "ext/filters/client_channel/resolver/dns/dns_resolver_plugin.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/resolver/dns/dns_resolver_plugin.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "experiments",
        "grpc_resolver_dns_event_engine",
        "grpc_resolver_dns_native",
        "//:config",
        "//:config_vars",
        "//:gpr",
        "//:grpc_resolver",
        "//:grpc_resolver_dns_ares",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_native",
    srcs = [
        "ext/filters/client_channel/resolver/dns/native/dns_resolver.cc",
    ],
    hdrs = [
        "ext/filters/client_channel/resolver/dns/native/dns_resolver.h",
    ],
    external_deps = [
        "absl/functional:bind_front",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "polling_resolver",
        "resolved_address",
        "time",
        "//:backoff",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_resolver",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_sockaddr",
    srcs = [
        "ext/filters/client_channel/resolver/sockaddr/sockaddr_resolver.cc",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "iomgr_port",
        "resolved_address",
        "//:config",
        "//:gpr",
        "//:grpc_resolver",
        "//:orphanable",
        "//:parse_address",
        "//:server_address",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_binder",
    srcs = [
        "ext/filters/client_channel/resolver/binder/binder_resolver.cc",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "error",
        "iomgr_port",
        "resolved_address",
        "status_helper",
        "//:config",
        "//:gpr",
        "//:grpc_resolver",
        "//:orphanable",
        "//:server_address",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_xds_header",
    hdrs = [
        "ext/filters/client_channel/resolver/xds/xds_resolver.h",
    ],
    external_deps = ["absl/strings"],
    language = "c++",
    deps = [
        "grpc_service_config",
        "unique_type_name",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_xds",
    srcs = [
        "ext/filters/client_channel/resolver/xds/xds_resolver.cc",
    ],
    external_deps = [
        "absl/meta:type_traits",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
        "absl/types:variant",
        "re2",
        "xxhash",
    ],
    language = "c++",
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "context",
        "dual_ref_counted",
        "grpc_lb_policy_ring_hash",
        "grpc_resolver_xds_header",
        "grpc_service_config",
        "grpc_xds_client",
        "iomgr_fwd",
        "match",
        "pollset_set",
        "ref_counted",
        "slice",
        "time",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_public_hdrs",
        "//:grpc_resolver",
        "//:grpc_service_config_impl",
        "//:grpc_trace",
        "//:legacy_context",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server_address",
        "//:uri_parser",
        "//:work_serializer",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_c2p",
    srcs = [
        "ext/filters/client_channel/resolver/google_c2p/google_c2p_resolver.cc",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "env",
        "gcp_metadata_query",
        "grpc_xds_client",
        "json",
        "json_writer",
        "resource_quota",
        "time",
        "//:alts_util",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_resolver",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri_parser",
        "//:work_serializer",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "hpack_constants",
    hdrs = [
        "ext/transport/chttp2/transport/hpack_constants.h",
    ],
    language = "c++",
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "hpack_encoder_table",
    srcs = [
        "ext/transport/chttp2/transport/hpack_encoder_table.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/hpack_encoder_table.h",
    ],
    external_deps = ["absl/container:inlined_vector"],
    language = "c++",
    deps = [
        "hpack_constants",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "chttp2_flow_control",
    srcs = [
        "ext/transport/chttp2/transport/flow_control.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/flow_control.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    deps = [
        "bdp_estimator",
        "experiments",
        "http2_settings",
        "memory_quota",
        "pid_controller",
        "time",
        "useful",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "huffsyms",
    srcs = [
        "ext/transport/chttp2/transport/huffsyms.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/huffsyms.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "decode_huff",
    srcs = [
        "ext/transport/chttp2/transport/decode_huff.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/decode_huff.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "http2_settings",
    srcs = [
        "ext/transport/chttp2/transport/http2_settings.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/http2_settings.h",
    ],
    deps = [
        "http2_errors",
        "useful",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_alpn",
    srcs = [
        "ext/transport/chttp2/alpn/alpn.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/alpn/alpn.h",
    ],
    language = "c++",
    deps = [
        "useful",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_client_connector",
    srcs = [
        "ext/transport/chttp2/client/chttp2_connector.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/client/chttp2_connector.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "channel_args_endpoint_config",
        "channel_args_preconditioning",
        "channel_stack_type",
        "closure",
        "error",
        "grpc_insecure_credentials",
        "handshaker_registry",
        "resolved_address",
        "status_helper",
        "tcp_connect_handshaker",
        "time",
        "transport_fwd",
        "unique_type_name",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_public_hdrs",
        "//:grpc_resolver",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:grpc_transport_chttp2",
        "//:handshaker",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_server",
    srcs = [
        "ext/transport/chttp2/server/chttp2_server.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/server/chttp2_server.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "channel_args",
        "channel_args_endpoint_config",
        "closure",
        "error",
        "grpc_insecure_credentials",
        "handshaker_registry",
        "iomgr_fwd",
        "memory_quota",
        "pollset_set",
        "resolved_address",
        "resource_quota",
        "status_helper",
        "time",
        "transport_fwd",
        "unique_type_name",
        "//:chttp2_frame",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:grpc_transport_chttp2",
        "//:handshaker",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_transport_inproc",
    srcs = [
        "ext/transport/inproc/inproc_plugin.cc",
        "ext/transport/inproc/inproc_transport.cc",
    ],
    hdrs = [
        "ext/transport/inproc/inproc_transport.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    deps = [
        "arena",
        "channel_args",
        "channel_args_preconditioning",
        "channel_stack_type",
        "closure",
        "error",
        "iomgr_fwd",
        "slice",
        "slice_buffer",
        "status_helper",
        "time",
        "transport_fwd",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "chaotic_good_frame",
    srcs = [
        "ext/transport/chaotic_good/frame.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/frame.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/types:variant",
    ],
    deps = [
        "arena",
        "bitset",
        "chaotic_good_frame_header",
        "no_destruct",
        "slice",
        "slice_buffer",
        "status_helper",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:hpack_encoder",
        "//:hpack_parser",
    ],
)

grpc_cc_library(
    name = "chaotic_good_frame_header",
    srcs = [
        "ext/transport/chaotic_good/frame_header.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/frame_header.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "bitset",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "gcp_metadata_query",
    srcs = [
        "ext/gcp/metadata_query.cc",
    ],
    hdrs = [
        "ext/gcp/metadata_query.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "closure",
        "error",
        "status_helper",
        "time",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:httpcli",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "logging_sink",
    hdrs = [
        "ext/filters/logging/logging_sink.h",
    ],
    external_deps = [
        "absl/numeric:int128",
        "absl/strings",
    ],
    language = "c++",
    visibility = [
        "//src/cpp/ext/gcp:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "time",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "logging_filter",
    srcs = [
        "ext/filters/logging/logging_filter.cc",
    ],
    hdrs = [
        "ext/filters/logging/logging_filter.h",
    ],
    external_deps = [
        "absl/numeric:int128",
        "absl/random",
        "absl/random:distributions",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "c++",
    visibility = [
        "//src/cpp/ext/gcp:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "arena",
        "arena_promise",
        "cancel_callback",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "context",
        "logging_sink",
        "map",
        "pipe",
        "poll",
        "slice",
        "slice_buffer",
        "time",
        "//:channel_stack_builder",
        "//:config",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_public_hdrs",
        "//:grpc_resolver",
        "//:legacy_context",
        "//:uri_parser",
    ],
)

grpc_cc_library(
    name = "grpc_promise_endpoint",
    srcs = [
        "lib/transport/promise_endpoint.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/types:optional",
    ],
    language = "c++",
    public_hdrs = [
        "lib/transport/promise_endpoint.h",
    ],
    deps = [
        "activity",
        "event_engine_common",
        "poll",
        "slice",
        "slice_buffer",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "chaotic_good_client_transport",
    srcs = [
        "ext/transport/chaotic_good/client_transport.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/client_transport.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/status",
        "absl/status:statusor",
        "absl/time",
        "absl/types:variant",
    ],
    language = "c++",
    deps = [
        "activity",
        "basic_seq",
        "chaotic_good_frame",
        "chaotic_good_frame_header",
        "event_engine_wakeup_scheduler",
        "for_each",
        "grpc_promise_endpoint",
        "join",
        "loop",
        "match",
        "mpsc",
        "seq",
        "slice",
        "slice_buffer",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:hpack_encoder",
    ],
)

### UPB Targets

grpc_upb_proto_library(
    name = "envoy_admin_upb",
    deps = ["@envoy_api//envoy/admin/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_cluster_upb",
    deps = ["@envoy_api//envoy/config/cluster/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_config_cluster_upbdefs",
    deps = ["@envoy_api//envoy/config/cluster/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_core_upb",
    deps = ["@envoy_api//envoy/config/core/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_endpoint_upb",
    deps = ["@envoy_api//envoy/config/endpoint/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_config_endpoint_upbdefs",
    deps = ["@envoy_api//envoy/config/endpoint/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_listener_upb",
    deps = ["@envoy_api//envoy/config/listener/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_config_listener_upbdefs",
    deps = ["@envoy_api//envoy/config/listener/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_rbac_upb",
    deps = ["@envoy_api//envoy/config/rbac/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_config_route_upb",
    deps = ["@envoy_api//envoy/config/route/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_config_route_upbdefs",
    deps = ["@envoy_api//envoy/config/route/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_clusters_aggregate_upb",
    deps = ["@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_clusters_aggregate_upbdefs",
    deps = ["@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_common_fault_upb",
    deps = ["@envoy_api//envoy/extensions/filters/common/fault/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_http_fault_upb",
    deps = ["@envoy_api//envoy/extensions/filters/http/fault/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_http_fault_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/http/fault/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_http_rbac_upb",
    deps = ["@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_http_rbac_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_http_router_upb",
    deps = ["@envoy_api//envoy/extensions/filters/http/router/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_http_router_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/http/router/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_http_stateful_session_upb",
    deps = ["@envoy_api//envoy/extensions/filters/http/stateful_session/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_http_stateful_session_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/http/stateful_session/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_http_stateful_session_cookie_upb",
    deps = ["@envoy_api//envoy/extensions/http/stateful_session/cookie/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_http_stateful_session_cookie_upbdefs",
    deps = ["@envoy_api//envoy/extensions/http/stateful_session/cookie/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_type_http_upb",
    deps = ["@envoy_api//envoy/type/http/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_load_balancing_policies_client_side_weighted_round_robin_upb",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_load_balancing_policies_ring_hash_upb",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/ring_hash/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_load_balancing_policies_wrr_locality_upb",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/wrr_locality/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_load_balancing_policies_pick_first_upb",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/pick_first/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_filters_network_http_connection_manager_upb",
    deps = ["@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_network_http_connection_manager_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_transport_sockets_tls_upb",
    deps = ["@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_transport_sockets_tls_upbdefs",
    deps = ["@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_service_discovery_upb",
    deps = ["@envoy_api//envoy/service/discovery/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_service_discovery_upbdefs",
    deps = ["@envoy_api//envoy/service/discovery/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_service_load_stats_upb",
    deps = ["@envoy_api//envoy/service/load_stats/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_service_load_stats_upbdefs",
    deps = ["@envoy_api//envoy/service/load_stats/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_service_status_upb",
    deps = ["@envoy_api//envoy/service/status/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_service_status_upbdefs",
    deps = ["@envoy_api//envoy/service/status/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_type_matcher_upb",
    deps = ["@envoy_api//envoy/type/matcher/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_type_upb",
    deps = ["@envoy_api//envoy/type/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_type_upb",
    deps = ["@com_github_cncf_udpa//xds/type/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "xds_type_upbdefs",
    deps = ["@com_github_cncf_udpa//xds/type/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_orca_upb",
    deps = ["@com_github_cncf_udpa//xds/data/orca/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_orca_service_upb",
    deps = ["@com_github_cncf_udpa//xds/service/orca/v3:pkg"],
)

grpc_upb_proto_library(
    name = "grpc_health_upb",
    deps = ["//src/proto/grpc/health/v1:health_proto_descriptor"],
)

grpc_upb_proto_library(
    name = "google_rpc_status_upb",
    deps = ["@com_google_googleapis//google/rpc:status_proto"],
)

grpc_upb_proto_reflection_library(
    name = "google_rpc_status_upbdefs",
    deps = ["@com_google_googleapis//google/rpc:status_proto"],
)

grpc_upb_proto_library(
    name = "google_type_expr_upb",
    deps = ["@com_google_googleapis//google/type:expr_proto"],
)

grpc_upb_proto_library(
    name = "grpc_lb_upb",
    deps = ["//src/proto/grpc/lb/v1:load_balancer_proto_descriptor"],
)

grpc_upb_proto_library(
    name = "alts_upb",
    deps = ["//src/proto/grpc/gcp:alts_handshaker_proto"],
)

grpc_upb_proto_library(
    name = "rls_upb",
    deps = ["//src/proto/grpc/lookup/v1:rls_proto_descriptor"],
)

grpc_upb_proto_library(
    name = "rls_config_upb",
    deps = ["//src/proto/grpc/lookup/v1:rls_config_proto_descriptor"],
)

grpc_upb_proto_reflection_library(
    name = "rls_config_upbdefs",
    deps = ["//src/proto/grpc/lookup/v1:rls_config_proto_descriptor"],
)

WELL_KNOWN_PROTO_TARGETS = [
    "any",
    "duration",
    "empty",
    "struct",
    "timestamp",
    "wrappers",
]

[grpc_upb_proto_library(
    name = "protobuf_" + target + "_upb",
    deps = ["@com_google_protobuf//:" + target + "_proto"],
) for target in WELL_KNOWN_PROTO_TARGETS]

[grpc_upb_proto_reflection_library(
    name = "protobuf_" + target + "_upbdefs",
    deps = ["@com_google_protobuf//:" + target + "_proto"],
) for target in WELL_KNOWN_PROTO_TARGETS]

grpc_generate_one_off_internal_targets()
