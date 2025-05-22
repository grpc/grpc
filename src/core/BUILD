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
    "grpc_cc_proto_library",
    "grpc_generate_one_off_internal_targets",
    "grpc_internal_proto_library",
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

config_setting(
    name = "enable_latent_see",
    values = {"define": "GRPC_ENABLE_LATENT_SEE=1"},
)

config_setting(
    name = "force_unsecure_getenv",
    values = {"define": "GRPC_FORCE_UNSECURE_GETENV=1"},
)

# This is needed as a transitionary mechanism to build the src/core targets in
# the top-level BUILD file that have not yet been moved here. Should go away
# once the transition is complete.
exports_files(
    glob(["**"]),
    visibility = ["//:__subpackages__"],
)

config_setting(
    name = "maximize_threadyness",
    values = {"define": "GRPC_MAXIMIZE_THREADYNESS=1"},
)

grpc_cc_library(
    name = "channel_fwd",
    hdrs = [
        "lib/channel/channel_fwd.h",
    ],
)

grpc_cc_library(
    name = "dump_args",
    srcs = [
        "util/dump_args.cc",
    ],
    hdrs = [
        "util/dump_args.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log:check",
        "absl/strings",
        "absl/strings:str_format",
    ],
)

grpc_cc_library(
    name = "slice_cast",
    hdrs = [
        "//:include/grpc/event_engine/internal/slice_cast.h",
    ],
)

grpc_cc_library(
    name = "event_engine_extensions",
    hdrs = [
        "lib/event_engine/extensions/blocking_dns.h",
        "lib/event_engine/extensions/can_track_errors.h",
        "lib/event_engine/extensions/channelz.h",
        "lib/event_engine/extensions/chaotic_good_extension.h",
        "lib/event_engine/extensions/iomgr_compatible.h",
        "lib/event_engine/extensions/supports_fd.h",
        "lib/event_engine/extensions/supports_win_sockets.h",
        "lib/event_engine/extensions/tcp_trace.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status:statusor",
        "absl/strings",
    ],
    visibility = [
        "//bazel:event_engine_base_hdrs",
    ],
    deps = [
        "memory_quota",
        "tcp_tracer",
        "//:channelz",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:ref_counted_ptr",
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
        "lib/event_engine/extensions/can_track_errors.h",
        "lib/event_engine/handle_containers.h",
        "lib/event_engine/resolved_address_internal.h",
        "//:include/grpc/event_engine/slice.h",
        "//:include/grpc/event_engine/slice_buffer.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/hash",
        "absl/log:check",
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
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "transport_framing_endpoint_extension",
    hdrs = [
        "lib/transport/transport_framing_endpoint_extension.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/strings",
    ],
    deps = [
        "slice_buffer",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "latent_see",
    srcs = [
        "util/latent_see.cc",
    ],
    hdrs = [
        "util/latent_see.h",
    ],
    defines = select({
        ":enable_latent_see": ["GRPC_ENABLE_LATENT_SEE"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:any_invocable",
        "absl/functional:function_ref",
        "absl/log",
        "absl/strings",
        "absl/strings:str_format",
    ],
    visibility = ["//bazel:latent_see"],
    deps = [
        "per_cpu",
        "ring_buffer",
        "sync",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "ring_buffer",
    srcs = [],
    hdrs = [
        "util/ring_buffer.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "transport_fwd",
    hdrs = [
        "lib/transport/transport_fwd.h",
    ],
)

grpc_cc_library(
    name = "server_call_tracer_filter",
    srcs = [
        "server/server_call_tracer_filter.cc",
    ],
    hdrs = [
        "server/server_call_tracer_filter.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "arena_promise",
        "call_finalization",
        "cancel_callback",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "context",
        "latent_see",
        "map",
        "pipe",
        "//:call_tracer",
        "//:config",
        "//:gpr_platform",
        "//:grpc_base",
    ],
)

grpc_cc_library(
    name = "atomic_utils",
    public_hdrs = ["util/atomic_utils.h"],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "metadata_compression_traits",
    hdrs = [
        "call/metadata_compression_traits.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "metadata_info",
    srcs = ["call/metadata_info.cc"],
    hdrs = ["call/metadata_info.h"],
    external_deps = [
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "hpack_constants",
        "metadata_batch",
        "slice",
        "//:call_tracer",
        "//:gpr_platform",
    ],
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
    defines = select(
        {
            "//:grpc_experiments_are_final": ["GRPC_EXPERIMENTS_ARE_FINAL"],
            "//conditions:default": [],
        },
    ),
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    visibility = ["//bazel:grpc_experiments"],
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
    hdrs = ["util/useful.h"],
    external_deps = [
        "absl/log:check",
        "absl/numeric:bits",
        "absl/strings",
    ],
    visibility = ["//bazel:useful"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "unique_ptr_with_bitset",
    hdrs = ["util/unique_ptr_with_bitset.h"],
    external_deps = [
        "absl/log:check",
        "absl/numeric:bits",
    ],
)

grpc_cc_library(
    name = "examine_stack",
    srcs = [
        "util/examine_stack.cc",
    ],
    hdrs = [
        "util/examine_stack.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "gpr_atm",
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
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "gpr_manual_constructor",
    srcs = [],
    hdrs = [
        "util/manual_constructor.h",
    ],
    deps = [
        "construct_destruct",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "gpr_spinlock",
    srcs = [],
    hdrs = [
        "util/spinlock.h",
    ],
    deps = [
        "gpr_atm",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "gpr_time",
    srcs = [
        "util/gpr_time.cc",
        "util/posix/time.cc",
        "util/windows/time.cc",
    ],
    hdrs = [
    ],
    external_deps = ["absl/log:check"],
    tags = [
        "nofixdeps",
    ],
    deps = [
        "gpr_atm",
        "time_precise",
        "//:gpr_platform",
        "//:gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "time_precise",
    srcs = ["util/time_precise.cc"],
    hdrs = [
        "util/time_precise.h",
    ],
    external_deps = ["absl/log"],
    tags = [
        "nofixdeps",
    ],
    deps = [
        "//:gpr_platform",
        "//:gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "time_util",
    srcs = [
        "util/time_util.cc",
    ],
    hdrs = [
        "util/time_util.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/time",
    ],
    tags = [
        "nofixdeps",
    ],
    deps = [
        "//:gpr_platform",
        "//:gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "sync",
    srcs = [
        "//src/core:util/posix/sync.cc",
        "//src/core:util/sync.cc",
        "//src/core:util/sync_abseil.cc",
        "//src/core:util/windows/sync.cc",
    ],
    hdrs = [
        "util/sync.h",
    ],
    external_deps = [
        "absl/base",
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/log:check",
        "absl/synchronization",
        "absl/time",
    ],
    tags = [
        "nofixdeps",
    ],
    deps = [
        "gpr_atm",
        "time_util",
        "//:gpr_platform",
        "//:gpr_public_hdrs",
    ],
)

grpc_cc_library(
    name = "env",
    srcs = [
        "util/linux/env.cc",
        "util/posix/env.cc",
        "util/windows/env.cc",
    ],
    hdrs = [
        "util/env.h",
    ],
    defines = select({
        ":force_unsecure_getenv": ["GRPC_FORCE_UNSECURE_GETENV"],
        "//conditions:default": [],
    }),
    deps = [
        "tchar",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "directory_reader",
    srcs = [
        "util/posix/directory_reader.cc",
        "util/windows/directory_reader.cc",
    ],
    hdrs = [
        "util/directory_reader.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "chunked_vector",
    hdrs = ["util/chunked_vector.h"],
    external_deps = ["absl/log:check"],
    deps = [
        "arena",
        "gpr_manual_constructor",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "construct_destruct",
    public_hdrs = ["util/construct_destruct.h"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "status_helper",
    srcs = [
        "util/status_helper.cc",
    ],
    hdrs = [
        "util/status_helper.h",
    ],
    external_deps = [
        "@com_google_protobuf//upb:base",
        "@com_google_protobuf//upb:mem",
        "absl/log:check",
        "absl/status",
        "absl/strings",
        "absl/strings:cord",
        "absl/time",
    ],
    deps = [
        "experiments",
        "percent_encoding",
        "slice",
        "status_conversion",
        "time",
        "//:debug_location",
        "//:google_rpc_status_upb",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//:protobuf_any_upb",
    ],
)

grpc_cc_library(
    name = "unique_type_name",
    hdrs = ["util/unique_type_name.h"],
    external_deps = ["absl/strings"],
    deps = [
        "useful",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "validation_errors",
    srcs = [
        "util/validation_errors.cc",
    ],
    hdrs = [
        "util/validation_errors.h",
    ],
    external_deps = [
        "absl/log",
        "absl/status",
        "absl/strings",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "overload",
    public_hdrs = ["util/overload.h"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "match",
    public_hdrs = ["util/match.h"],
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
    public_hdrs = ["util/table.h"],
    deps = [
        "bitset",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "packed_table",
    hdrs = ["util/packed_table.h"],
    deps = [
        "sorted_pack",
        "table",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "bitset",
    public_hdrs = ["util/bitset.h"],
    deps = [
        "useful",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "seq_bit_set",
    srcs = ["util/seq_bit_set.cc"],
    hdrs = ["util/seq_bit_set.h"],
    external_deps = [
        "absl/strings",
    ],
)

grpc_cc_library(
    name = "no_destruct",
    public_hdrs = ["util/no_destruct.h"],
    deps = [
        "construct_destruct",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "tchar",
    srcs = [
        "util/tchar.cc",
    ],
    hdrs = [
        "util/tchar.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "poll",
    external_deps = [
        "absl/log:check",
        "absl/strings:str_format",
        "absl/strings",
    ],
    public_hdrs = [
        "lib/promise/poll.h",
    ],
    deps = [
        "construct_destruct",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "status_flag",
    external_deps = [
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    public_hdrs = [
        "lib/promise/status_flag.h",
    ],
    deps = [
        "promise_status",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "map_pipe",
    external_deps = [
        "absl/log",
        "absl/status",
    ],
    public_hdrs = [
        "lib/promise/map_pipe.h",
    ],
    deps = [
        "for_each",
        "map",
        "pipe",
        "poll",
        "promise_factory",
        "try_seq",
        "//:gpr_platform",
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
    defines = select({
        ":maximize_threadyness": ["GRPC_MAXIMIZE_THREADYNESS"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "activity",
        "arena",
        "check_class_size",
        "construct_destruct",
        "context",
        "event_engine_context",
        "latent_see",
        "poll",
        "promise_factory",
        "ref_counted",
        "sync",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_trace",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "context",
    external_deps = [
        "absl/log:check",
        "absl/meta:type_traits",
    ],
    public_hdrs = [
        "lib/promise/context.h",
    ],
    deps = [
        "down_cast",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "map",
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    public_hdrs = ["lib/promise/map.h"],
    deps = [
        "poll",
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "promise_variant",
    public_hdrs = ["lib/promise/detail/promise_variant.h"],
)

grpc_cc_library(
    name = "match_promise",
    public_hdrs = ["lib/promise/match_promise.h"],
    deps = [
        "overload",
        "promise_factory",
        "promise_like",
        "promise_variant",
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
        "event_engine_context",
        "poll",
        "time",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "wait_for_callback",
    hdrs = [
        "lib/promise/wait_for_callback.h",
    ],
    external_deps = ["absl/base:core_headers"],
    deps = [
        "activity",
        "poll",
        "sync",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "arena_promise",
    external_deps = ["absl/meta:type_traits"],
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
    external_deps = [
        "absl/functional:any_invocable",
        "absl/meta:type_traits",
    ],
    public_hdrs = [
        "lib/promise/detail/promise_like.h",
    ],
    deps = [
        "poll",
        "//:gpr_platform",
        "//src/core:function_signature",
        "//src/core:json",
    ],
)

grpc_cc_library(
    name = "cancel_callback",
    public_hdrs = [
        "lib/promise/cancel_callback.h",
    ],
    deps = [
        "arena",
        "context",
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "promise_factory",
    external_deps = ["absl/meta:type_traits"],
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
    external_deps = ["absl/status:statusor"],
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
    name = "switch",
    public_hdrs = ["lib/promise/switch.h"],
    deps = [
        "if",
        "promise_factory",
        "promise_variant",
        "//:gpr",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "promise_status",
    external_deps = [
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
    ],
    public_hdrs = [
        "lib/promise/detail/status.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "race",
    public_hdrs = ["lib/promise/race.h"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "prioritized_race",
    public_hdrs = ["lib/promise/prioritized_race.h"],
    deps = [
        "promise_like",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "loop",
    external_deps = [
        "absl/status",
        "absl/status:statusor",
    ],
    public_hdrs = [
        "lib/promise/loop.h",
    ],
    deps = [
        "construct_destruct",
        "poll",
        "promise_factory",
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "join_state",
    external_deps = [
        "absl/log",
        "absl/log:check",
    ],
    public_hdrs = [
        "lib/promise/detail/join_state.h",
    ],
    deps = [
        "bitset",
        "construct_destruct",
        "poll",
        "promise_like",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "join",
    external_deps = ["absl/meta:type_traits"],
    public_hdrs = [
        "lib/promise/join.h",
    ],
    deps = [
        "join_state",
        "map",
        "promise_factory",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "try_join",
    external_deps = [
        "absl/log:check",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
    ],
    public_hdrs = [
        "lib/promise/try_join.h",
    ],
    deps = [
        "join_state",
        "map",
        "poll",
        "status_flag",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "all_ok",
    external_deps = [
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
    ],
    public_hdrs = [
        "lib/promise/all_ok.h",
    ],
    deps = [
        "join_state",
        "map",
        "poll",
        "promise_factory",
        "status_flag",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "basic_seq",
    public_hdrs = [
        "lib/promise/detail/basic_seq.h",
    ],
    deps = [
        "construct_destruct",
        "poll",
        "promise_factory",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "seq_state",
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/log:check",
        "absl/strings",
    ],
    public_hdrs = [
        "lib/promise/detail/seq_state.h",
    ],
    deps = [
        "construct_destruct",
        "json",
        "poll",
        "promise_factory",
        "promise_like",
        "//:debug_location",
        "//:gpr",
        "//:grpc_trace",
        "//:promise",
    ],
)

grpc_cc_library(
    name = "seq",
    public_hdrs = [
        "lib/promise/seq.h",
    ],
    deps = [
        "basic_seq",
        "poll",
        "promise_like",
        "seq_state",
        "//:debug_location",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "try_seq",
    external_deps = [
        "absl/log:check",
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
    ],
    public_hdrs = [
        "lib/promise/try_seq.h",
    ],
    deps = [
        "basic_seq",
        "poll",
        "promise_like",
        "promise_status",
        "seq_state",
        "status_flag",
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
        "absl/log:check",
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
    ],
    public_hdrs = [
        "lib/promise/activity.h",
    ],
    deps = [
        "atomic_utils",
        "construct_destruct",
        "context",
        "dump_args",
        "latent_see",
        "no_destruct",
        "poll",
        "promise_factory",
        "promise_status",
        "sync",
        "//:gpr",
        "//:grpc_trace",
        "//:orphanable",
    ],
)

grpc_cc_library(
    name = "exec_ctx_wakeup_scheduler",
    hdrs = [
        "lib/promise/exec_ctx_wakeup_scheduler.h",
    ],
    external_deps = ["absl/status"],
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
    external_deps = ["absl/log:check"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "wait_set",
    srcs = [
        "lib/promise/wait_set.cc",
    ],
    hdrs = [
        "lib/promise/wait_set.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/strings",
        "absl/hash",
    ],
    deps = [
        "activity",
        "poll",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "latch",
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/strings",
    ],
    public_hdrs = [
        "lib/promise/latch.h",
    ],
    deps = [
        "activity",
        "poll",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "inter_activity_latch",
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/strings",
    ],
    public_hdrs = [
        "lib/promise/inter_activity_latch.h",
    ],
    deps = [
        "activity",
        "poll",
        "sync",
        "wait_set",
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
        "absl/log",
        "absl/log:check",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "arena",
        "construct_destruct",
        "context",
        "poll",
        "promise_factory",
        "//:debug_location",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "pipe",
    hdrs = [
        "lib/promise/pipe.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/strings",
    ],
    deps = [
        "activity",
        "arena",
        "context",
        "if",
        "interceptor_list",
        "map",
        "poll",
        "seq",
        "//:debug_location",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "promise_mutex",
    hdrs = [
        "lib/promise/promise_mutex.h",
    ],
    external_deps = ["absl/log:check"],
    deps = [
        "activity",
        "poll",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "inter_activity_mutex",
    hdrs = [
        "lib/promise/inter_activity_mutex.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/log",
    ],
    deps = [
        "activity",
        "dump_args",
        "poll",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "inter_activity_pipe",
    hdrs = [
        "lib/promise/inter_activity_pipe.h",
    ],
    external_deps = ["absl/base:core_headers"],
    deps = [
        "activity",
        "gpr_manual_constructor",
        "poll",
        "ref_counted",
        "sync",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "mpsc",
    hdrs = [
        "lib/promise/mpsc.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:check",
    ],
    deps = [
        "activity",
        "dump_args",
        "poll",
        "ref_counted",
        "status_flag",
        "sync",
        "wait_set",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "observable",
    hdrs = [
        "lib/promise/observable.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/functional:any_invocable",
        "absl/log:check",
    ],
    deps = [
        "activity",
        "poll",
        "sync",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "for_each",
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings",
    ],
    public_hdrs = ["lib/promise/for_each.h"],
    deps = [
        "activity",
        "construct_destruct",
        "poll",
        "promise_factory",
        "promise_status",
        "status_flag",
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "ref_counted",
    external_deps = [
        "absl/log",
        "absl/log:check",
    ],
    public_hdrs = ["util/ref_counted.h"],
    deps = [
        "atomic_utils",
        "down_cast",
        "//:debug_location",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "dual_ref_counted",
    external_deps = [
        "absl/log",
        "absl/log:check",
    ],
    public_hdrs = ["util/dual_ref_counted.h"],
    deps = [
        "down_cast",
        "ref_counted",
        "//:debug_location",
        "//:gpr",
        "//:orphanable",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "ref_counted_string",
    srcs = [
        "util/ref_counted_string.cc",
    ],
    hdrs = [
        "util/ref_counted_string.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "ref_counted",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "uuid_v4",
    srcs = ["util/uuid_v4.cc"],
    external_deps = ["absl/strings:str_format"],
    public_hdrs = ["util/uuid_v4.h"],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "handshaker_factory",
    public_hdrs = [
        "handshaker/handshaker_factory.h",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "channel_args",
        "iomgr_fwd",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "handshaker_registry",
    srcs = [
        "handshaker/handshaker_registry.cc",
    ],
    public_hdrs = [
        "handshaker/handshaker_registry.h",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
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
        "handshaker/tcp_connect/tcp_connect_handshaker.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:any_invocable",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
    ],
    public_hdrs = [
        "handshaker/tcp_connect/tcp_connect_handshaker.h",
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
        "sync",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:handshaker",
        "//:iomgr",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "endpoint_info_handshaker",
    srcs = [
        "handshaker/endpoint_info/endpoint_info_handshaker.cc",
    ],
    hdrs = [
        "handshaker/endpoint_info/endpoint_info_handshaker.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
    ],
    deps = [
        "channel_args",
        "closure",
        "handshaker_factory",
        "handshaker_registry",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:handshaker",
        "//:iomgr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "channel_creds_registry",
    hdrs = [
        "credentials/transport/channel_creds_registry.h",
    ],
    external_deps = ["absl/strings"],
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
    hdrs = [
        "//:include/grpc/event_engine/internal/memory_allocator_impl.h",
        "//:include/grpc/event_engine/memory_allocator.h",
        "//:include/grpc/event_engine/memory_request.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "slice",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "event_engine_memory_allocator_factory",
    hdrs = [
        "lib/event_engine/memory_allocator_factory.h",
    ],
    external_deps = ["absl/strings"],
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "activity",
        "event_engine_memory_allocator",
        "exec_ctx_wakeup_scheduler",
        "experiments",
        "loop",
        "map",
        "periodic_update",
        "poll",
        "race",
        "seq",
        "slice_refcount",
        "sync",
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
    external_deps = ["absl/log"],
    visibility = [
        "//bazel:alt_grpc_base_legacy",
    ],
    deps = [
        "construct_destruct",
        "context",
        "event_engine_memory_allocator",
        "memory_quota",
        "resource_quota",
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
    external_deps = [
        "absl/base:core_headers",
        "absl/log:check",
    ],
    deps = [
        "ref_counted",
        "sync",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "connection_quota",
    srcs = [
        "lib/resource_quota/connection_quota.cc",
    ],
    hdrs = [
        "lib/resource_quota/connection_quota.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:check",
    ],
    deps = [
        "memory_quota",
        "ref_counted",
        "sync",
        "//:gpr",
        "//:ref_counted_ptr",
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
        "//bazel:alt_grpc_base_legacy",
    ],
    deps = [
        "memory_quota",
        "ref_counted",
        "thread_quota",
        "useful",
        "//:channel_arg_names",
        "//:cpp_impl_of",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "request_buffer",
    srcs = [
        "call/request_buffer.cc",
    ],
    hdrs = [
        "call/request_buffer.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "call_spine",
        "match",
        "message",
        "metadata",
    ],
)

grpc_cc_library(
    name = "slice_refcount",
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
        "absl/log:check",
        "absl/strings",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
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
    external_deps = ["absl/log:check"],
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "experiments",
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
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/strings:str_format",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
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
        "util/time.cc",
    ],
    hdrs = [
        "util/time.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/strings:str_format",
    ],
    deps = [
        "no_destruct",
        "time_precise",
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
    visibility = ["//bazel:alt_grpc_base_legacy"],
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
    external_deps = ["absl/log:check"],
    deps = [
        "iomgr_port",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "avl",
    hdrs = [
        "util/avl.h",
    ],
    deps = [
        "ref_counted",
        "useful",
        "//:gpr_platform",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "time_averaged_stats",
    srcs = ["util/time_averaged_stats.cc"],
    hdrs = [
        "util/time_averaged_stats.h",
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
    external_deps = ["absl/log:check"],
    deps = [
        "//:config_vars",
        "//:gpr_platform",
        "//:grpc_trace",
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
    name = "event_engine_query_extensions",
    hdrs = [
        "lib/event_engine/query_extensions.h",
    ],
    external_deps = ["absl/strings"],
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
        "sync",
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
    external_deps = ["absl/base:core_headers"],
    deps = [
        "sync",
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
    name = "event_engine_thread_count",
    srcs = [
        "lib/event_engine/thread_pool/thread_count.cc",
    ],
    hdrs = ["lib/event_engine/thread_pool/thread_count.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/status",
        "absl/strings:str_format",
        "absl/time",
    ],
    deps = [
        "sync",
        "time",
        "useful",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "event_engine_thread_pool",
    srcs = [
        "lib/event_engine/thread_pool/thread_pool_factory.cc",
        "lib/event_engine/thread_pool/work_stealing_thread_pool.cc",
    ],
    hdrs = [
        "lib/event_engine/thread_pool/thread_pool.h",
        "lib/event_engine/thread_pool/work_stealing_thread_pool.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_set",
        "absl/functional:any_invocable",
        "absl/log",
        "absl/log:check",
        "absl/time",
    ],
    deps = [
        "common_event_engine_closures",
        "env",
        "event_engine_basic_work_queue",
        "event_engine_thread_count",
        "event_engine_thread_local",
        "event_engine_work_queue",
        "examine_stack",
        "forkable",
        "no_destruct",
        "notification",
        "sync",
        "time",
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
    deps = [
        "event_engine_extensions",
        "event_engine_query_extensions",
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
        "absl/log",
        "absl/log:check",
        "absl/time",
    ],
    deps = [
        "event_engine_thread_pool",
        "forkable",
        "notification",
        "posix_event_engine_timer",
        "sync",
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
        "forkable",
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
    external_deps = [
        "absl/log:check",
        "absl/status",
    ],
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "event_engine_poller",
        "event_engine_time_util",
        "iomgr_port",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "posix_event_engine_internal_errqueue",
        "posix_event_engine_lockfree_event",
        "posix_event_engine_wakeup_fd_posix",
        "posix_event_engine_wakeup_fd_posix_default",
        "status_helper",
        "strerror",
        "sync",
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
        "absl/log:check",
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
        "sync",
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
        "forkable",
        "iomgr_port",
        "no_destruct",
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
    external_deps = ["absl/log"],
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
        "absl/log",
        "absl/status",
    ],
    deps = [
        "iomgr_port",
        "posix_event_engine_internal_errqueue",
        "sync",
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "event_engine_common",
        "event_engine_extensions",
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
        "sync",
        "time",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
        "//:ref_counted_ptr",
        "//:stats",
    ],
)

grpc_cc_library(
    name = "event_engine_utils",
    srcs = ["lib/event_engine/utils.cc"],
    hdrs = ["lib/event_engine/utils.h"],
    external_deps = [
        "absl/log",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "event_engine_extensions",
        "event_engine_query_extensions",
        "notification",
        "time",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "posix_event_engine_tcp_socket_utils",
    srcs = [
        "lib/event_engine/posix_engine/set_socket_dualstack.cc",
        "lib/event_engine/posix_engine/tcp_socket_utils.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/tcp_socket_utils.h",
    ],
    external_deps = [
        "absl/cleanup",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
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
        "//:channel_arg_names",
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
        "absl/log",
        "absl/log:check",
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
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
        "strerror",
        "sync",
        "time",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_trace",
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
        "absl/log",
        "absl/log:check",
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
        "event_engine_utils",
        "experiments",
        "forkable",
        "init_internally",
        "iomgr_port",
        "native_posix_dns_resolver",
        "no_destruct",
        "posix_event_engine_base_hdrs",
        "posix_event_engine_closure",
        "posix_event_engine_endpoint",
        "posix_event_engine_event_poller",
        "posix_event_engine_listener",
        "posix_event_engine_poller_posix_default",
        "posix_event_engine_tcp_socket_utils",
        "posix_event_engine_timer",
        "posix_event_engine_timer_manager",
        "ref_counted_dns_resolver_interface",
        "sync",
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "ares_resolver",
        "channel_args_endpoint_config",
        "common_event_engine_closures",
        "dump_args",
        "error",
        "event_engine_common",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
        "event_engine_utils",
        "init_internally",
        "iomgr_port",
        "posix_event_engine_timer_manager",
        "sync",
        "time",
        "windows_endpoint",
        "windows_event_engine_listener",
        "windows_iocp",
        "windows_native_resolver",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "windows_native_resolver",
    srcs = ["lib/event_engine/windows/native_windows_dns_resolver.cc"],
    hdrs = ["lib/event_engine/windows/native_windows_dns_resolver.h"],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "error",
        "status_helper",
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings:str_format",
    ],
    deps = [
        "error",
        "event_engine_poller",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
        "event_engine_time_util",
        "sync",
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings:str_format",
    ],
    deps = [
        "error",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
    ],
    deps = [
        "common_event_engine_closures",
        "error",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
        "iomgr_port",
        "sync",
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
        "lib/event_engine/cf_engine/dns_service_resolver.cc",
    ],
    hdrs = [
        "lib/event_engine/cf_engine/cf_engine.h",
        "lib/event_engine/cf_engine/cfstream_endpoint.h",
        "lib/event_engine/cf_engine/cftype_unique_ref.h",
        "lib/event_engine/cf_engine/dns_service_resolver.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "event_engine_common",
        "event_engine_tcp_socket_utils",
        "event_engine_thread_pool",
        "event_engine_utils",
        "init_internally",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "posix_event_engine_lockfree_event",
        "posix_event_engine_timer_manager",
        "ref_counted",
        "strerror",
        "sync",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:parse_address",
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "event_engine_common",
        "iomgr_port",
        "resolved_address",
        "status_helper",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:gpr_platform",
        "//:parse_address",
        "//:uri",
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
    select_deps = [
        {
            "//:windows": ["windows_event_engine"],
            "//:mac": [
                "posix_event_engine",
                "cf_event_engine",
            ],
            "//:ios": ["cf_event_engine"],
            "//:tvos": ["cf_event_engine"],
            "//:visionos": ["cf_event_engine"],
            "//:watchos": ["cf_event_engine"],
            "//conditions:default": ["posix_event_engine"],
        },
    ],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "channel_args_endpoint_config",
    srcs = [
        "lib/event_engine/channel_args_endpoint_config.cc",
    ],
    hdrs = [
        "lib/event_engine/channel_args_endpoint_config.h",
    ],
    external_deps = ["absl/strings"],
    visibility = ["//bazel:alt_grpc_base_legacy"],
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
        "sync",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "event_engine_context",
    hdrs = [
        "lib/event_engine/event_engine_context.h",
    ],
    visibility = [
        "//bazel:alt_grpc_base_legacy",
    ],
    deps = [
        "arena",
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
    defines = select({
        ":maximize_threadyness": ["GRPC_MAXIMIZE_THREADYNESS"],
        "//conditions:default": [],
    }),
    external_deps = ["absl/functional:any_invocable"],
    visibility = [
        "//bazel:alt_grpc_base_legacy",
    ],
    deps = [
        "channel_args",
        "default_event_engine_factory",
        "match",
        "no_destruct",
        "sync",
        "thready_event_engine",
        "wait_for_single_owner",
        "//:config",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "ref_counted_dns_resolver_interface",
    hdrs = ["lib/event_engine/ref_counted_dns_resolver_interface.h"],
    external_deps = ["absl/strings"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:orphanable",
    ],
)

grpc_cc_library(
    name = "native_posix_dns_resolver",
    srcs = [
        "lib/event_engine/posix_engine/native_posix_dns_resolver.cc",
    ],
    hdrs = [
        "lib/event_engine/posix_engine/native_posix_dns_resolver.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "iomgr_port",
        "useful",
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "ares_resolver",
    srcs = [
        "lib/event_engine/ares_resolver.cc",
        "lib/event_engine/windows/grpc_polled_fd_windows.cc",
    ],
    hdrs = [
        "lib/event_engine/ares_resolver.h",
        "lib/event_engine/grpc_polled_fd.h",
        "lib/event_engine/nameser.h",
        "lib/event_engine/posix_engine/grpc_polled_fd_posix.h",
        "lib/event_engine/windows/grpc_polled_fd_windows.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/hash",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "address_sorting",
        "cares",
    ],
    deps = [
        "common_event_engine_closures",
        "error",
        "event_engine_time_util",
        "grpc_sockaddr",
        "iomgr_port",
        "posix_event_engine_closure",
        "posix_event_engine_event_poller",
        "posix_event_engine_tcp_socket_utils",
        "ref_counted_dns_resolver_interface",
        "resolved_address",
        "slice",
        "sync",
        "windows_iocp",
        "//:config_vars",
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
    name = "bdp_estimator",
    srcs = [
        "lib/transport/bdp_estimator.cc",
    ],
    hdrs = ["lib/transport/bdp_estimator.h"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/strings",
    ],
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
    external_deps = ["absl/log:check"],
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
    visibility = ["//bazel:alt_grpc_base_legacy"],
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
        "telemetry/histogram_view.cc",
    ],
    hdrs = [
        "telemetry/histogram_view.h",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "stats_data",
    srcs = ["telemetry/stats_data.cc"],
    hdrs = ["telemetry/stats_data.h"],
    external_deps = ["absl/strings"],
    deps = [
        "histogram_view",
        "no_destruct",
        "per_cpu",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "per_cpu",
    srcs = [
        "util/per_cpu.cc",
    ],
    hdrs = [
        "util/per_cpu.h",
    ],
    deps = [
        "useful",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "event_log",
    srcs = [
        "util/event_log.cc",
    ],
    hdrs = [
        "util/event_log.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:check",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "per_cpu",
        "sync",
        "time_precise",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "load_file",
    srcs = [
        "util/load_file.cc",
    ],
    hdrs = [
        "util/load_file.h",
    ],
    external_deps = [
        "absl/cleanup",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "slice",
        "strerror",
        "//:gpr",
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
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log",
        "absl/log:check",
        "absl/strings",
    ],
    deps = [
        "call_filters",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "interception_chain",
        "sync",
        "unique_type_name",
        "//:channel_stack_builder",
        "//:debug_location",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "server_interface",
    hdrs = [
        "server/server_interface.h",
    ],
    deps = [
        "channel_args",
        "//:channelz",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "single_set_ptr",
    hdrs = [
        "util/single_set_ptr.h",
    ],
    external_deps = ["absl/log:check"],
    deps = [
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_service_config",
    hdrs = [
        "service_config/service_config.h",
        "service_config/service_config_call_data.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "arena",
        "chunked_vector",
        "down_cast",
        "ref_counted",
        "service_config_parser",
        "slice_refcount",
        "unique_type_name",
        "useful",
        "//:gpr_platform",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "service_config_parser",
    srcs = [
        "service_config/service_config_parser.cc",
    ],
    hdrs = [
        "service_config/service_config_parser.h",
    ],
    external_deps = [
        "absl/log",
        "absl/strings",
    ],
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
        "util/notification.h",
    ],
    external_deps = ["absl/time"],
    deps = [
        "sync",
        "//:gpr",
    ],
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
        "absl/log",
        "absl/log:check",
        "absl/meta:type_traits",
        "absl/strings",
        "absl/strings:str_format",
    ],
    visibility = [
        "//bazel:alt_grpc_base_legacy",
    ],
    deps = [
        "avl",
        "channel_stack_type",
        "dual_ref_counted",
        "ref_counted",
        "ref_counted_string",
        "time",
        "useful",
        "//:channel_arg_names",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "resolved_address",
    hdrs = ["lib/iomgr/resolved_address.h"],
    deps = [
        "iomgr_port",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "client_channel_internal_header",
    hdrs = [
        "client_channel/client_channel_internal.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log:check",
    ],
    deps = [
        "arena",
        "call_destination",
        "down_cast",
        "grpc_service_config",
        "lb_policy",
        "unique_type_name",
        "//:call_tracer",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "blackboard",
    srcs = [
        "filter/blackboard.cc",
    ],
    hdrs = [
        "filter/blackboard.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/strings",
    ],
    deps = [
        "ref_counted",
        "unique_type_name",
        "useful",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "filter_args",
    hdrs = [
        "filter/filter_args.h",
    ],
    deps = [
        "blackboard",
        "channel_fwd",
        "match",
    ],
)

grpc_cc_library(
    name = "subchannel_connector",
    hdrs = [
        "client_channel/connector.h",
    ],
    deps = [
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "resolved_address",
        "time",
        "//:channelz",
        "//:gpr_platform",
        "//:grpc_base",
        "//:orphanable",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "subchannel_pool_interface",
    srcs = [
        "client_channel/subchannel_pool_interface.cc",
    ],
    hdrs = [
        "client_channel/subchannel_pool_interface.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "ref_counted",
        "resolved_address",
        "useful",
        "//:gpr_platform",
        "//:grpc_trace",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "config_selector",
    hdrs = [
        "client_channel/config_selector.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "arena",
        "channel_fwd",
        "client_channel_internal_header",
        "grpc_service_config",
        "interception_chain",
        "metadata_batch",
        "ref_counted",
        "slice",
        "unique_type_name",
        "useful",
        "//:gpr_platform",
        "//:grpc_public_hdrs",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "client_channel_service_config",
    srcs = [
        "client_channel/client_channel_service_config.cc",
    ],
    hdrs = [
        "client_channel/client_channel_service_config.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_registry",
        "service_config_parser",
        "time",
        "validation_errors",
        "//:config",
        "//:gpr_platform",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "client_channel_args",
    hdrs = [
        "client_channel/client_channel_args.h",
    ],
)

grpc_cc_library(
    name = "retry_interceptor",
    srcs = [
        "client_channel/retry_interceptor.cc",
    ],
    hdrs = [
        "client_channel/retry_interceptor.h",
    ],
    deps = [
        "cancel_callback",
        "client_channel_args",
        "filter_args",
        "for_each",
        "grpc_service_config",
        "interception_chain",
        "map",
        "request_buffer",
        "retry_service_config",
        "retry_throttle",
        "sleep",
        "//:backoff",
    ],
)

grpc_cc_library(
    name = "retry_service_config",
    srcs = [
        "client_channel/retry_service_config.cc",
    ],
    hdrs = [
        "client_channel/retry_service_config.h",
    ],
    external_deps = [
        "absl/log",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "json",
        "json_args",
        "json_channel_args",
        "json_object_loader",
        "service_config_parser",
        "time",
        "validation_errors",
        "//:channel_arg_names",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "retry_throttle",
    srcs = [
        "client_channel/retry_throttle.cc",
    ],
    hdrs = [
        "client_channel/retry_throttle.h",
    ],
    external_deps = ["absl/base:core_headers"],
    deps = [
        "ref_counted",
        "sync",
        "useful",
        "//:gpr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "client_channel_backup_poller",
    srcs = [
        "client_channel/backup_poller.cc",
    ],
    hdrs = [
        "client_channel/backup_poller.h",
    ],
    external_deps = [
        "absl/log",
        "absl/status",
    ],
    deps = [
        "closure",
        "error",
        "iomgr_fwd",
        "pollset_set",
        "time",
        "//:config_vars",
        "//:gpr",
        "//:gpr_platform",
        "//:iomgr",
        "//:iomgr_timer",
    ],
)

grpc_cc_library(
    name = "service_config_channel_arg_filter",
    srcs = [
        "service_config/service_config_channel_arg_filter.cc",
    ],
    external_deps = [
        "absl/log",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "context",
        "grpc_message_size_filter",
        "grpc_service_config",
        "latent_see",
        "metadata_batch",
        "service_config_parser",
        "//:channel_arg_names",
        "//:config",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_service_config_impl",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "lb_policy",
    srcs = ["load_balancing/lb_policy.cc"],
    hdrs = ["load_balancing/lb_policy.h"],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "closure",
        "dual_ref_counted",
        "error",
        "grpc_backend_metric_data",
        "iomgr_fwd",
        "metrics",
        "pollset_set",
        "ref_counted",
        "resolved_address",
        "subchannel_interface",
        "sync",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr_platform",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "lb_policy_factory",
    hdrs = ["load_balancing/lb_policy_factory.h"],
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
    srcs = ["load_balancing/lb_policy_registry.cc"],
    hdrs = ["load_balancing/lb_policy_registry.h"],
    external_deps = [
        "absl/log",
        "absl/log:check",
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
    name = "lb_metadata",
    srcs = ["client_channel/lb_metadata.cc"],
    hdrs = ["client_channel/lb_metadata.h"],
    external_deps = [
        "absl/log",
        "absl/strings",
    ],
    deps = [
        "event_engine_common",
        "lb_policy",
        "metadata_batch",
    ],
)

grpc_cc_library(
    name = "connection_context",
    srcs = ["lib/surface/connection_context.cc"],
    hdrs = ["lib/surface/connection_context.h"],
    deps = [
        "no_destruct",
        "//:gpr",
        "//:gpr_platform",
        "//:orphanable",
    ],
)

grpc_cc_library(
    name = "subchannel_interface",
    hdrs = ["load_balancing/subchannel_interface.h"],
    external_deps = [
        "absl/status",
        "absl/strings",
    ],
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
    hdrs = ["load_balancing/delegating_helper.h"],
    external_deps = [
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "lb_policy",
        "resolved_address",
        "subchannel_interface",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:grpc_security_base",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
    ],
)

grpc_cc_library(
    name = "backend_metric_parser",
    srcs = [
        "load_balancing/backend_metric_parser.cc",
    ],
    hdrs = [
        "load_balancing/backend_metric_parser.h",
    ],
    external_deps = [
        "@com_google_protobuf//upb:base",
        "@com_google_protobuf//upb:mem",
        "@com_google_protobuf//upb:message",
        "absl/strings",
    ],
    deps = [
        "grpc_backend_metric_data",
        "//:gpr_platform",
        "//:xds_orca_upb",
    ],
)

grpc_cc_library(
    name = "proxy_mapper",
    hdrs = ["handshaker/proxy_mapper.h"],
    external_deps = ["absl/strings"],
    deps = [
        "channel_args",
        "resolved_address",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "proxy_mapper_registry",
    srcs = ["handshaker/proxy_mapper_registry.cc"],
    hdrs = ["handshaker/proxy_mapper_registry.h"],
    external_deps = ["absl/strings"],
    deps = [
        "channel_args",
        "proxy_mapper",
        "resolved_address",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "http_proxy_mapper",
    srcs = [
        "handshaker/http_connect/http_proxy_mapper.cc",
    ],
    hdrs = [
        "handshaker/http_connect/http_proxy_mapper.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "env",
        "proxy_mapper",
        "resolved_address",
        "//:channel_arg_names",
        "//:config",
        "//:gpr",
        "//:http_connect_handshaker",
        "//:iomgr",
        "//:parse_address",
        "//:sockaddr_utils",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "xds_http_proxy_mapper",
    srcs = [
        "handshaker/http_connect/xds_http_proxy_mapper.cc",
    ],
    hdrs = [
        "handshaker/http_connect/xds_http_proxy_mapper.h",
    ],
    external_deps = [
        "absl/log",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "proxy_mapper",
        "resolved_address",
        "xds_endpoint",
        "//:config",
        "//:http_connect_handshaker",
        "//:parse_address",
        "//:sockaddr_utils",
    ],
)

grpc_cc_library(
    name = "grpc_server_config_selector",
    hdrs = [
        "server/server_config_selector.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "dual_ref_counted",
        "grpc_service_config",
        "metadata_batch",
        "ref_counted",
        "service_config_parser",
        "useful",
        "//:gpr_platform",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "grpc_server_config_selector_filter",
    srcs = [
        "server/server_config_selector_filter.cc",
    ],
    hdrs = [
        "server/server_config_selector_filter.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "context",
        "event_engine_context",
        "grpc_server_config_selector",
        "grpc_service_config",
        "latent_see",
        "metadata_batch",
        "status_helper",
        "sync",
        "//:gpr",
        "//:grpc_base",
        "//:promise",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "sorted_pack",
    hdrs = [
        "util/sorted_pack.h",
    ],
    deps = [
        "type_list",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "type_list",
    hdrs = [
        "util/type_list.h",
    ],
)

grpc_cc_library(
    name = "if_list",
    hdrs = [
        "util/if_list.h",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "tdigest",
    srcs = [
        "util/tdigest.cc",
    ],
    hdrs = [
        "util/tdigest.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings",
    ],
)

grpc_cc_library(
    name = "certificate_provider_factory",
    hdrs = [
        "credentials/transport/tls/certificate_provider_factory.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "json",
        "json_args",
        "ref_counted",
        "validation_errors",
        "//:alts_util",
        "//:gpr",
        "//:grpc_core_credentials_header",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "certificate_provider_registry",
    srcs = [
        "credentials/transport/tls/certificate_provider_registry.cc",
    ],
    hdrs = [
        "credentials/transport/tls/certificate_provider_registry.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/strings",
    ],
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
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
    ],
    deps = [
        "sync",
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
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "dual_ref_counted",
        "endpoint_info_handshaker",
        "latent_see",
        "metadata_batch",
        "ref_counted",
        "resolved_address",
        "slice",
        "useful",
        "//:channel_arg_names",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_credentials_util",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:parse_address",
        "//:promise",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "grpc_crl_provider",
    srcs = [
        "credentials/transport/tls/grpc_tls_crl_provider.cc",
    ],
    hdrs = [
        "credentials/transport/tls/grpc_tls_crl_provider.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:span",
        "libcrypto",
        "libssl",
    ],
    deps = [
        "default_event_engine",
        "directory_reader",
        "load_file",
        "slice",
        "sync",
        "time",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
    ],
)

grpc_cc_library(
    name = "grpc_fake_credentials",
    srcs = [
        "credentials/transport/fake/fake_credentials.cc",
        "credentials/transport/fake/fake_security_connector.cc",
    ],
    hdrs = [
        "credentials/transport/fake/fake_credentials.h",
        "credentials/transport/fake/fake_security_connector.h",
        "load_balancing/grpclb/grpclb.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "metadata_batch",
        "slice",
        "unique_type_name",
        "useful",
        "//:channel_arg_names",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//:handshaker",
        "//:iomgr",
        "//:promise",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:tsi_base",
        "//:tsi_fake_credentials",
    ],
)

grpc_cc_library(
    name = "grpc_insecure_credentials",
    srcs = [
        "credentials/transport/insecure/insecure_credentials.cc",
        "credentials/transport/insecure/insecure_security_connector.cc",
    ],
    hdrs = [
        "credentials/transport/insecure/insecure_credentials.h",
        "credentials/transport/insecure/insecure_security_connector.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/status",
        "absl/strings",
    ],
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
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//:handshaker",
        "//:iomgr",
        "//:promise",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
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
    external_deps = ["absl/log"],
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
        "credentials/transport/local/local_credentials.cc",
        "credentials/transport/local/local_security_connector.cc",
    ],
    hdrs = [
        "credentials/transport/local/local_credentials.h",
        "credentials/transport/local/local_security_connector.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "experiments",
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
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//:handshaker",
        "//:iomgr",
        "//:parse_address",
        "//:promise",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:transport_auth_context",
        "//:tsi_base",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "grpc_ssl_credentials",
    srcs = [
        "credentials/transport/ssl/ssl_credentials.cc",
        "credentials/transport/ssl/ssl_security_connector.cc",
    ],
    hdrs = [
        "credentials/transport/ssl/ssl_credentials.h",
        "credentials/transport/ssl/ssl_security_connector.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "sync",
        "unique_type_name",
        "useful",
        "//:channel_arg_names",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:handshaker",
        "//:iomgr",
        "//:promise",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:tsi_base",
        "//:tsi_ssl_credentials",
        "//:tsi_ssl_session_cache",
    ],
)

grpc_cc_library(
    name = "grpc_google_default_credentials",
    srcs = [
        "credentials/transport/google_default/credentials_generic.cc",
        "credentials/transport/google_default/google_default_credentials.cc",
    ],
    hdrs = [
        "credentials/transport/google_default/google_default_credentials.h",
        "load_balancing/grpclb/grpclb.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status:statusor",
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    deps = [
        "channel_args",
        "closure",
        "env",
        "error",
        "error_utils",
        "event_engine_shim",
        "grpc_external_account_credentials",
        "grpc_lb_xds_channel_args",
        "grpc_oauth2_credentials",
        "grpc_ssl_credentials",
        "iomgr_fwd",
        "json",
        "json_reader",
        "load_file",
        "notification",
        "slice",
        "slice_refcount",
        "status_helper",
        "sync",
        "time",
        "unique_type_name",
        "useful",
        "//:alts_util",
        "//:channel_arg_names",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_alts_credentials",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_jwt_credentials",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:httpcli",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "strerror",
    srcs = [
        "util/strerror.cc",
    ],
    hdrs = [
        "util/strerror.h",
    ],
    external_deps = ["absl/strings:str_format"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_tls_credentials",
    srcs = [
        "credentials/transport/tls/grpc_tls_certificate_distributor.cc",
        "credentials/transport/tls/grpc_tls_certificate_match.cc",
        "credentials/transport/tls/grpc_tls_certificate_provider.cc",
        "credentials/transport/tls/grpc_tls_certificate_verifier.cc",
        "credentials/transport/tls/grpc_tls_credentials_options.cc",
        "credentials/transport/tls/tls_credentials.cc",
        "credentials/transport/tls/tls_security_connector.cc",
    ],
    hdrs = [
        "credentials/transport/tls/grpc_tls_certificate_distributor.h",
        "credentials/transport/tls/grpc_tls_certificate_provider.h",
        "credentials/transport/tls/grpc_tls_certificate_verifier.h",
        "credentials/transport/tls/grpc_tls_credentials_options.h",
        "credentials/transport/tls/tls_credentials.h",
        "credentials/transport/tls/tls_security_connector.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/functional:bind_front",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "libcrypto",
        "libssl",
    ],
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "load_file",
        "ref_counted",
        "slice",
        "slice_refcount",
        "status_helper",
        "sync",
        "unique_type_name",
        "useful",
        "//:channel_arg_names",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_credentials_util",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:handshaker",
        "//:iomgr",
        "//:promise",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:tsi_base",
        "//:tsi_ssl_credentials",
        "//:tsi_ssl_session_cache",
    ],
)

grpc_cc_library(
    name = "grpc_iam_credentials",
    srcs = [
        "credentials/call/iam/iam_credentials.cc",
    ],
    hdrs = [
        "credentials/call/iam/iam_credentials.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "arena_promise",
        "metadata_batch",
        "slice",
        "unique_type_name",
        "useful",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:promise",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
    ],
)

grpc_cc_library(
    name = "token_fetcher_credentials",
    srcs = [
        "credentials/call/token_fetcher/token_fetcher_credentials.cc",
    ],
    hdrs = [
        "credentials/call/token_fetcher/token_fetcher_credentials.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/functional:any_invocable",
        "absl/status:statusor",
    ],
    deps = [
        "arena_promise",
        "context",
        "default_event_engine",
        "metadata",
        "poll",
        "pollset_set",
        "ref_counted",
        "sync",
        "time",
        "useful",
        "//:backoff",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:httpcli",
        "//:iomgr",
        "//:orphanable",
        "//:promise",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
    ],
)

grpc_cc_library(
    name = "gcp_service_account_identity_credentials",
    srcs = [
        "credentials/call/gcp_service_account_identity/gcp_service_account_identity_credentials.cc",
    ],
    hdrs = [
        "credentials/call/gcp_service_account_identity/gcp_service_account_identity_credentials.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "closure",
        "error",
        "json",
        "json_args",
        "json_object_loader",
        "json_reader",
        "metadata",
        "slice",
        "status_conversion",
        "status_helper",
        "time",
        "token_fetcher_credentials",
        "unique_type_name",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//:httpcli",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "grpc_oauth2_credentials",
    srcs = [
        "credentials/call/oauth2/oauth2_credentials.cc",
    ],
    hdrs = [
        "credentials/call/oauth2/oauth2_credentials.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "activity",
        "arena_promise",
        "closure",
        "context",
        "error",
        "error_utils",
        "httpcli_ssl_credentials",
        "json",
        "json_reader",
        "load_file",
        "metadata_batch",
        "poll",
        "pollset_set",
        "ref_counted",
        "slice",
        "slice_refcount",
        "status_helper",
        "time",
        "token_fetcher_credentials",
        "unique_type_name",
        "useful",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_credentials_util",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:httpcli",
        "//:iomgr",
        "//:orphanable",
        "//:promise",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "grpc_external_account_credentials",
    srcs = [
        "credentials/call/external/aws_external_account_credentials.cc",
        "credentials/call/external/aws_request_signer.cc",
        "credentials/call/external/external_account_credentials.cc",
        "credentials/call/external/file_external_account_credentials.cc",
        "credentials/call/external/url_external_account_credentials.cc",
    ],
    hdrs = [
        "credentials/call/external/aws_external_account_credentials.h",
        "credentials/call/external/aws_request_signer.h",
        "credentials/call/external/external_account_credentials.h",
        "credentials/call/external/file_external_account_credentials.h",
        "credentials/call/external/url_external_account_credentials.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "libcrypto",
    ],
    deps = [
        "closure",
        "env",
        "error",
        "error_utils",
        "grpc_oauth2_credentials",
        "httpcli_ssl_credentials",
        "json",
        "json_reader",
        "json_writer",
        "load_file",
        "slice",
        "slice_refcount",
        "status_helper",
        "time",
        "token_fetcher_credentials",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_credentials_util",
        "//:grpc_security_base",
        "//:httpcli",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "httpcli_ssl_credentials",
    srcs = [
        "util/http_client/httpcli_security_connector.cc",
    ],
    hdrs = [
        "util/http_client/httpcli_ssl_credentials.h",
    ],
    external_deps = [
        "absl/log",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "arena_promise",
        "channel_args",
        "closure",
        "error",
        "iomgr_fwd",
        "unique_type_name",
        "//:channel_arg_names",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//:handshaker",
        "//:iomgr",
        "//:promise",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
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
    deps = ["//:gpr_platform"],
)

# This target depends on RE2 and should not be linked into grpc by default for binary-size reasons.
grpc_cc_library(
    name = "grpc_matchers",
    srcs = [
        "util/matchers.cc",
    ],
    hdrs = [
        "util/matchers.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "re2",
    ],
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
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
        "util/json/json.h",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "json_reader",
    srcs = [
        "util/json/json_reader.cc",
    ],
    hdrs = [
        "util/json/json_reader.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    visibility = ["//bazel:json_reader_legacy"],
    deps = [
        "json",
        "match",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "json_writer",
    srcs = [
        "util/json/json_writer.cc",
    ],
    hdrs = [
        "util/json/json_writer.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "json",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "json_util",
    srcs = ["util/json/json_util.cc"],
    hdrs = ["util/json/json_util.h"],
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
    hdrs = ["util/json/json_args.h"],
    external_deps = ["absl/strings"],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "json_object_loader",
    srcs = ["util/json/json_object_loader.cc"],
    hdrs = ["util/json/json_object_loader.h"],
    external_deps = [
        "absl/meta:type_traits",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
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
    hdrs = ["util/json/json_channel_args.h"],
    external_deps = ["absl/strings"],
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
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_channel_idle_filter",
    srcs = [
        "ext/filters/channel_idle/legacy_channel_idle_filter.cc",
    ],
    hdrs = [
        "ext/filters/channel_idle/legacy_channel_idle_filter.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/meta:type_traits",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "activity",
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "closure",
        "connectivity_state",
        "error",
        "exec_ctx_wakeup_scheduler",
        "experiments",
        "http2_status",
        "idle_filter_state",
        "loop",
        "metadata_batch",
        "no_destruct",
        "per_cpu",
        "poll",
        "shared_bit_gen",
        "single_set_ptr",
        "sleep",
        "status_helper",
        "sync",
        "time",
        "try_seq",
        "//:channel_arg_names",
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
    ],
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "latent_see",
        "metadata_batch",
        "slice",
        "//:channel_arg_names",
        "//:config",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:transport_auth_context",
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
        "absl/log",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "activity",
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "context",
        "grpc_service_config",
        "json",
        "json_args",
        "json_object_loader",
        "latch",
        "latent_see",
        "metadata_batch",
        "race",
        "service_config_parser",
        "slice",
        "slice_buffer",
        "validation_errors",
        "//:channel_arg_names",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
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
        "absl/log",
        "absl/meta:type_traits",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "context",
        "grpc_service_config",
        "json",
        "json_args",
        "json_object_loader",
        "metadata_batch",
        "service_config_parser",
        "sleep",
        "sync",
        "time",
        "try_seq",
        "validation_errors",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
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
    ],
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
        "latent_see",
        "metadata_batch",
        "service_config_parser",
        "validation_errors",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:promise",
        "//:transport_auth_context",
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
        "absl/log:check",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "context",
        "grpc_resolver_xds_attributes",
        "grpc_service_config",
        "json",
        "json_args",
        "json_object_loader",
        "latent_see",
        "map",
        "metadata_batch",
        "pipe",
        "ref_counted_string",
        "service_config_parser",
        "slice",
        "time",
        "unique_type_name",
        "validation_errors",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "gcp_authentication_filter",
    srcs = [
        "ext/filters/gcp_authentication/gcp_authentication_filter.cc",
        "ext/filters/gcp_authentication/gcp_authentication_service_config_parser.cc",
    ],
    hdrs = [
        "ext/filters/gcp_authentication/gcp_authentication_filter.h",
        "ext/filters/gcp_authentication/gcp_authentication_service_config_parser.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "arena",
        "blackboard",
        "channel_args",
        "channel_fwd",
        "context",
        "gcp_service_account_identity_credentials",
        "grpc_resolver_xds_attributes",
        "grpc_service_config",
        "json",
        "json_args",
        "json_object_loader",
        "lru_cache",
        "service_config_parser",
        "sync",
        "validation_errors",
        "xds_config",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_grpclb",
    srcs = [
        "load_balancing/grpclb/client_load_reporting_filter.cc",
        "load_balancing/grpclb/grpclb.cc",
        "load_balancing/grpclb/grpclb_client_stats.cc",
        "load_balancing/grpclb/load_balancer_api.cc",
    ],
    hdrs = [
        "load_balancing/grpclb/client_load_reporting_filter.h",
        "load_balancing/grpclb/grpclb.h",
        "load_balancing/grpclb/grpclb_client_stats.h",
        "load_balancing/grpclb/load_balancer_api.h",
    ],
    external_deps = [
        "@com_google_protobuf//upb:base",
        "@com_google_protobuf//upb:mem",
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/functional:function_ref",
        "absl/log",
        "absl/log:check",
        "absl/log:globals",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "closure",
        "connectivity_state",
        "context",
        "delegating_helper",
        "error",
        "experiments",
        "gpr_atm",
        "grpc_sockaddr",
        "json",
        "json_args",
        "json_object_loader",
        "latent_see",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "map",
        "metadata_batch",
        "pipe",
        "pollset_set",
        "ref_counted",
        "resolved_address",
        "slice",
        "slice_refcount",
        "status_helper",
        "subchannel_interface",
        "sync",
        "time",
        "useful",
        "validation_errors",
        "//:backoff",
        "//:channel",
        "//:channel_arg_names",
        "//:channelz",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
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
        "//:lb_child_policy_handler",
        "//:orphanable",
        "//:protobuf_duration_upb",
        "//:protobuf_timestamp_upb",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:transport_auth_context",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "random_early_detection",
    srcs = [
        "util/random_early_detection.cc",
    ],
    hdrs = [
        "util/random_early_detection.h",
    ],
    external_deps = [
        "absl/random:bit_gen_ref",
        "absl/random:distributions",
    ],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_backend_metric_data",
    hdrs = [
        "load_balancing/backend_metric_data.h",
    ],
    external_deps = ["absl/strings"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_backend_metric_provider",
    hdrs = [
        "ext/filters/backend_metrics/backend_metric_provider.h",
    ],
    deps = ["arena"],
)

grpc_cc_library(
    name = "grpc_lb_policy_rls",
    srcs = [
        "load_balancing/rls/rls.cc",
    ],
    hdrs = [
        "load_balancing/rls/rls.h",
    ],
    external_deps = [
        "@com_google_protobuf//upb:base",
        "@com_google_protobuf//upb:mem",
        "absl/base:core_headers",
        "absl/hash",
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "channel_args",
        "closure",
        "connectivity_state",
        "delegating_helper",
        "dual_ref_counted",
        "error",
        "error_utils",
        "grpc_fake_credentials",
        "json",
        "json_args",
        "json_object_loader",
        "json_writer",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "match",
        "metrics",
        "pollset_set",
        "shared_bit_gen",
        "slice",
        "slice_refcount",
        "status_helper",
        "sync",
        "time",
        "upb_utils",
        "uuid_v4",
        "validation_errors",
        "//:backoff",
        "//:channel",
        "//:channel_arg_names",
        "//:channelz",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_public_hdrs",
        "//:grpc_resolver",
        "//:grpc_security_base",
        "//:grpc_service_config_impl",
        "//:grpc_trace",
        "//:lb_child_policy_handler",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:rls_upb",
        "//:transport_auth_context",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "lru_cache",
    hdrs = [
        "util/lru_cache.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/log:check",
    ],
)

grpc_cc_library(
    name = "upb_utils",
    hdrs = [
        "util/upb_utils.h",
    ],
    external_deps = [
        "@com_google_protobuf//upb:base",
        "absl/strings",
    ],
)

grpc_cc_library(
    name = "xds_enabled_server",
    hdrs = [
        "xds/grpc/xds_enabled_server.h",
    ],
)

grpc_cc_library(
    name = "xds_certificate_provider",
    srcs = [
        "xds/grpc/xds_certificate_provider.cc",
    ],
    hdrs = [
        "xds/grpc/xds_certificate_provider.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:bind_front",
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    deps = [
        "channel_args",
        "error",
        "grpc_matchers",
        "grpc_tls_credentials",
        "sync",
        "unique_type_name",
        "useful",
        "//:gpr",
        "//:grpc_base",
        "//:ref_counted_ptr",
        "//:tsi_ssl_credentials",
    ],
)

grpc_cc_library(
    name = "xds_certificate_provider_store",
    srcs = [
        "xds/grpc/certificate_provider_store.cc",
    ],
    hdrs = [
        "xds/grpc/certificate_provider_store.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:log",
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    deps = [
        "certificate_provider_factory",
        "certificate_provider_registry",
        "grpc_tls_credentials",
        "json",
        "json_args",
        "json_object_loader",
        "sync",
        "unique_type_name",
        "useful",
        "validation_errors",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:orphanable",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "xds_credentials",
    srcs = [
        "credentials/transport/xds/xds_credentials.cc",
    ],
    hdrs = [
        "credentials/transport/xds/xds_credentials.h",
    ],
    external_deps = [
        "absl/status",
        "absl/log:check",
        "absl/log:log",
    ],
    tags = ["nofixdeps"],
    deps = [
        "channel_args",
        "grpc_lb_xds_channel_args",
        "grpc_matchers",
        "grpc_tls_credentials",
        "unique_type_name",
        "useful",
        "xds_certificate_provider",
        "//:channel_arg_names",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_credentials_util",
        "//:grpc_security_base",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
    ],
)

grpc_cc_library(
    name = "xds_file_watcher_certificate_provider_factory",
    srcs = [
        "xds/grpc/file_watcher_certificate_provider_factory.cc",
    ],
    hdrs = [
        "xds/grpc/file_watcher_certificate_provider_factory.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/strings",
        "absl/strings:str_format",
    ],
    tags = ["nofixdeps"],
    deps = [
        "certificate_provider_factory",
        "down_cast",
        "grpc_tls_credentials",
        "json",
        "json_args",
        "json_object_loader",
        "time",
        "validation_errors",
        "//:config",
        "//:gpr",
        "//:grpc_base",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "xds_common_types",
    srcs = [
        "xds/grpc/xds_common_types.cc",
    ],
    hdrs = [
        "xds/grpc/xds_common_types.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
    ],
    tags = ["nofixdeps"],
    deps = [
        "grpc_matchers",
        "json",
        "match",
        "validation_errors",
    ],
)

grpc_cc_library(
    name = "xds_http_filter",
    hdrs = [
        "xds/grpc/xds_http_filter.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "@com_google_protobuf//upb:reflection",
    ],
    tags = ["nofixdeps"],
    deps = [
        "channel_args",
        "channel_fwd",
        "interception_chain",
        "json",
        "json_writer",
        "validation_errors",
        "xds_common_types",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "xds_route_config",
    srcs = [
        "xds/grpc/xds_route_config.cc",
    ],
    hdrs = [
        "xds/grpc/xds_route_config.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
        "re2",
    ],
    tags = ["nofixdeps"],
    deps = [
        "grpc_matchers",
        "match",
        "time",
        "xds_http_filter",
        "//:grpc_base",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "xds_listener",
    srcs = [
        "xds/grpc/xds_listener.cc",
    ],
    hdrs = [
        "xds/grpc/xds_listener.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
    ],
    tags = ["nofixdeps"],
    deps = [
        "match",
        "resolved_address",
        "time",
        "xds_common_types",
        "xds_http_filter",
        "xds_route_config",
        "//:sockaddr_utils",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "xds_health_status",
    srcs = [
        "xds/grpc/xds_health_status.cc",
    ],
    hdrs = [
        "xds/grpc/xds_health_status.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/types:span",
    ],
    tags = ["nofixdeps"],
    deps = [
        "envoy_config_core_upb",
        "//:endpoint_addresses",
    ],
)

grpc_cc_library(
    name = "xds_server_grpc_interface",
    hdrs = [
        "xds/grpc/xds_server_grpc_interface.h",
    ],
    tags = ["nofixdeps"],
    visibility = ["//bazel:xds_client_core"],
    deps = [
        "channel_creds_registry",
        "//:ref_counted_ptr",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "xds_server_grpc",
    srcs = [
        "xds/grpc/xds_server_grpc.cc",
    ],
    hdrs = [
        "xds/grpc/xds_server_grpc.h",
    ],
    external_deps = [
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    deps = [
        "channel_creds_registry",
        "down_cast",
        "json",
        "json_args",
        "json_object_loader",
        "json_reader",
        "json_writer",
        "validation_errors",
        "xds_server_grpc_interface",
        "//:config",
        "//:ref_counted_ptr",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "xds_metadata",
    srcs = [
        "xds/grpc/xds_metadata.cc",
    ],
    hdrs = [
        "xds/grpc/xds_metadata.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/container:flat_hash_map",
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    deps = [
        "down_cast",
        "json",
        "json_writer",
        "validation_errors",
    ],
)

grpc_cc_library(
    name = "xds_cluster",
    srcs = [
        "xds/grpc/xds_cluster.cc",
    ],
    hdrs = [
        "xds/grpc/xds_cluster.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    deps = [
        "grpc_outlier_detection_header",
        "json",
        "json_writer",
        "match",
        "time",
        "xds_backend_metric_propagation",
        "xds_common_types",
        "xds_health_status",
        "xds_metadata",
        "xds_server_grpc",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "xds_endpoint",
    srcs = [
        "xds/grpc/xds_endpoint.cc",
    ],
    hdrs = [
        "xds/grpc/xds_endpoint.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/random",
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    deps = [
        "ref_counted",
        "sync",
        "//:endpoint_addresses",
        "//:gpr",
        "//:ref_counted_ptr",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "xds_backend_metric_propagation",
    srcs = [
        "xds/xds_client/xds_backend_metric_propagation.cc",
    ],
    hdrs = [
        "xds/xds_client/xds_backend_metric_propagation.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    visibility = ["//bazel:xds_client_core"],
    deps = [
        "ref_counted",
        "useful",
        "//:ref_counted_ptr",
    ],
)

# TODO(roth): Split this up into individual targets.
grpc_cc_library(
    name = "grpc_xds_client",
    srcs = [
        "xds/grpc/xds_audit_logger_registry.cc",
        "xds/grpc/xds_bootstrap_grpc.cc",
        "xds/grpc/xds_client_grpc.cc",
        "xds/grpc/xds_cluster_parser.cc",
        "xds/grpc/xds_cluster_specifier_plugin.cc",
        "xds/grpc/xds_common_types_parser.cc",
        "xds/grpc/xds_endpoint_parser.cc",
        "xds/grpc/xds_http_fault_filter.cc",
        "xds/grpc/xds_http_filter_registry.cc",
        "xds/grpc/xds_http_gcp_authn_filter.cc",
        "xds/grpc/xds_http_rbac_filter.cc",
        "xds/grpc/xds_http_stateful_session_filter.cc",
        "xds/grpc/xds_lb_policy_registry.cc",
        "xds/grpc/xds_listener_parser.cc",
        "xds/grpc/xds_metadata_parser.cc",
        "xds/grpc/xds_route_config_parser.cc",
        "xds/grpc/xds_routing.cc",
    ],
    hdrs = [
        "xds/grpc/xds_audit_logger_registry.h",
        "xds/grpc/xds_bootstrap_grpc.h",
        "xds/grpc/xds_client_grpc.h",
        "xds/grpc/xds_cluster_parser.h",
        "xds/grpc/xds_cluster_specifier_plugin.h",
        "xds/grpc/xds_common_types_parser.h",
        "xds/grpc/xds_endpoint_parser.h",
        "xds/grpc/xds_http_fault_filter.h",
        "xds/grpc/xds_http_filter_registry.h",
        "xds/grpc/xds_http_gcp_authn_filter.h",
        "xds/grpc/xds_http_rbac_filter.h",
        "xds/grpc/xds_http_stateful_session_filter.h",
        "xds/grpc/xds_lb_policy_registry.h",
        "xds/grpc/xds_listener_parser.h",
        "xds/grpc/xds_metadata_parser.h",
        "xds/grpc/xds_route_config_parser.h",
        "xds/grpc/xds_routing.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:flat_hash_map",
        "absl/functional:bind_front",
        "absl/log:check",
        "absl/log:log",
        "absl/memory",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/synchronization",
        "absl/types:span",
        "@com_google_protobuf//upb:base",
        "@com_google_protobuf//upb:mem",
        "@com_google_protobuf//upb:text",
        "@com_google_protobuf//upb:json",
        "re2",
        "@com_google_protobuf//upb:reflection",
        "@com_google_protobuf//upb:message",
    ],
    tags = ["nofixdeps"],
    deps = [
        "certificate_provider_factory",
        "certificate_provider_registry",
        "channel_args",
        "channel_args_endpoint_config",
        "channel_creds_registry",
        "channel_fwd",
        "closure",
        "connectivity_state",
        "default_event_engine",
        "down_cast",
        "env",
        "envoy_admin_upb",
        "envoy_config_cluster_upb",
        "envoy_config_cluster_upbdefs",
        "envoy_config_core_upb",
        "envoy_config_core_upbdefs",
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
        "envoy_extensions_filters_http_gcp_authn_upb",
        "envoy_extensions_filters_http_gcp_authn_upbdefs",
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
        "envoy_extensions_transport_sockets_http_11_proxy_upb",
        "envoy_extensions_transport_sockets_http_11_proxy_upbdefs",
        "envoy_extensions_transport_sockets_tls_upb",
        "envoy_extensions_transport_sockets_tls_upbdefs",
        "envoy_extensions_upstreams_http_upb",
        "envoy_extensions_upstreams_http_upbdefs",
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
        "error_utils",
        "gcp_authentication_filter",
        "google_rpc_status_upb",
        "grpc_audit_logging",
        "grpc_fake_credentials",
        "grpc_fault_injection_filter",
        "grpc_lb_policy_pick_first",
        "grpc_lb_policy_ring_hash",
        "grpc_lb_xds_channel_args",
        "grpc_matchers",
        "grpc_outlier_detection_header",
        "grpc_rbac_filter",
        "grpc_sockaddr",
        "grpc_stateful_session_filter",
        "grpc_tls_credentials",
        "grpc_transport_chttp2_client_connector",
        "init_internally",
        "interception_chain",
        "iomgr_fwd",
        "json",
        "json_args",
        "json_object_loader",
        "json_reader",
        "json_util",
        "json_writer",
        "lb_policy_registry",
        "load_file",
        "match",
        "metadata_batch",
        "metrics",
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
        "status_conversion",
        "status_helper",
        "sync",
        "time",
        "unique_type_name",
        "upb_utils",
        "useful",
        "validation_errors",
        "xds_backend_metric_propagation",
        "xds_certificate_provider",
        "xds_certificate_provider_store",
        "xds_cluster",
        "xds_common_types",
        "xds_credentials",
        "xds_endpoint",
        "xds_file_watcher_certificate_provider_factory",
        "xds_health_status",
        "xds_http_filter",
        "xds_listener",
        "xds_metadata",
        "xds_route_config",
        "xds_server_grpc",
        "xds_transport_grpc",
        "xds_type_upb",
        "xds_type_upbdefs",
        "//:channel",
        "//:channel_arg_names",
        "//:channel_create",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_core_credentials_header",
        "//:grpc_credentials_util",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:iomgr",
        "//:iomgr_timer",
        "//:orphanable",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:transport_auth_context",
        "//:tsi_ssl_credentials",
        "//:uri",
        "//:work_serializer",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "xds_transport_grpc",
    srcs = [
        "xds/grpc/xds_transport_grpc.cc",
    ],
    hdrs = [
        "xds/grpc/xds_transport_grpc.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/log:check",
        "absl/status",
        "absl/strings",
    ],
    tags = ["nofixdeps"],
    visibility = ["//bazel:xds_client_core"],
    deps = [
        "channel_args",
        "channel_creds_registry",
        "channel_fwd",
        "closure",
        "connectivity_state",
        "default_event_engine",
        "down_cast",
        "error",
        "init_internally",
        "iomgr_fwd",
        "metadata_batch",
        "pollset_set",
        "slice",
        "slice_refcount",
        "sync",
        "time",
        "useful",
        "xds_server_grpc_interface",
        "//:channel",
        "//:channel_arg_names",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_core_credentials_header",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:iomgr",
        "//:iomgr_timer",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_xds_channel_stack_modifier",
    srcs = [
        "server/xds_channel_stack_modifier.cc",
    ],
    hdrs = [
        "server/xds_channel_stack_modifier.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "channel_args",
        "channel_fwd",
        "channel_init",
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
        "server/xds_server_config_fetcher.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
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
        "metadata_batch",
        "resolved_address",
        "slice_refcount",
        "sync",
        "unique_type_name",
        "xds_certificate_provider",
        "xds_certificate_provider_store",
        "xds_common_types",
        "xds_credentials",
        "xds_http_filter",
        "xds_listener",
        "xds_route_config",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_service_config_impl",
        "//:grpc_trace",
        "//:iomgr",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:server",
        "//:sockaddr_utils",
        "//:transport_auth_context",
        "//:uri",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "channel_creds_registry_init",
    srcs = [
        "credentials/transport/channel_creds_registry_init.cc",
    ],
    external_deps = ["absl/strings"],
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
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_cds",
    srcs = [
        "load_balancing/xds/cds.cc",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "delegating_helper",
        "env",
        "grpc_lb_address_filtering",
        "grpc_lb_xds_channel_args",
        "grpc_outlier_detection_header",
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
        "xds_cluster",
        "xds_common_types",
        "xds_dependency_manager",
        "xds_health_status",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_xds_channel_args",
    hdrs = [
        "load_balancing/xds/xds_channel_args.h",
    ],
    deps = [
        "//:endpoint_addresses",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "shared_bit_gen",
    srcs = ["util/shared_bit_gen.cc"],
    hdrs = [
        "util/shared_bit_gen.h",
    ],
    external_deps = [
        "absl/random",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_impl",
    srcs = [
        "load_balancing/xds/xds_cluster_impl.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "client_channel_internal_header",
        "connectivity_state",
        "delegating_helper",
        "grpc_backend_metric_data",
        "grpc_lb_xds_channel_args",
        "grpc_resolver_xds_attributes",
        "grpc_xds_client",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "match",
        "pollset_set",
        "ref_counted",
        "ref_counted_string",
        "resolved_address",
        "subchannel_interface",
        "sync",
        "validation_errors",
        "xds_config",
        "xds_credentials",
        "xds_endpoint",
        "//:call_tracer",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:lb_child_policy_handler",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_cluster_manager",
    srcs = [
        "load_balancing/xds/xds_cluster_manager.cc",
    ],
    external_deps = [
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "client_channel_internal_header",
        "connectivity_state",
        "delegating_helper",
        "grpc_resolver_xds_attributes",
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
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_trace",
        "//:lb_child_policy_handler",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_wrr_locality",
    srcs = [
        "load_balancing/xds/xds_wrr_locality.cc",
    ],
    external_deps = [
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
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
        "ref_counted_string",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_lb_address_filtering",
    srcs = [
        "load_balancing/address_filtering.cc",
    ],
    hdrs = [
        "load_balancing/address_filtering.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "ref_counted",
        "ref_counted_string",
        "resolved_address",
        "//:endpoint_addresses",
        "//:gpr_platform",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "health_check_client",
    srcs = [
        "load_balancing/health_check_client.cc",
    ],
    hdrs = [
        "load_balancing/health_check_client.h",
        "load_balancing/health_check_client_internal.h",
    ],
    external_deps = [
        "@com_google_protobuf//upb:base",
        "@com_google_protobuf//upb:mem",
        "absl/base:core_headers",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "client_channel_internal_header",
        "closure",
        "connectivity_state",
        "error",
        "iomgr_fwd",
        "pollset_set",
        "slice",
        "subchannel_interface",
        "sync",
        "unique_type_name",
        "//:channel_arg_names",
        "//:channelz",
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
        "//:sockaddr_utils",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "lb_endpoint_list",
    srcs = [
        "load_balancing/endpoint_list.cc",
    ],
    hdrs = [
        "load_balancing/endpoint_list.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "channel_args",
        "delegating_helper",
        "down_cast",
        "grpc_lb_policy_pick_first",
        "json",
        "lb_policy",
        "lb_policy_registry",
        "pollset_set",
        "resolved_address",
        "subchannel_interface",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc_base",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_pick_first",
    srcs = [
        "load_balancing/pick_first/pick_first.cc",
    ],
    hdrs = [
        "load_balancing/pick_first/pick_first.h",
    ],
    external_deps = [
        "absl/algorithm:container",
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "connectivity_state",
        "experiments",
        "health_check_client",
        "iomgr_fwd",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "metrics",
        "resolved_address",
        "shared_bit_gen",
        "subchannel_interface",
        "time",
        "useful",
        "//:channel_arg_names",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "down_cast",
    hdrs = ["util/down_cast.h"],
    external_deps = [
        "absl/base:config",
        "absl/log:check",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "glob",
    srcs = ["util/glob.cc"],
    hdrs = ["util/glob.h"],
    external_deps = ["absl/strings"],
)

grpc_cc_library(
    name = "status_conversion",
    srcs = ["lib/transport/status_conversion.cc"],
    hdrs = ["lib/transport/status_conversion.h"],
    deps = [
        "http2_status",
        "time",
        "//:gpr_platform",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "error_utils",
    srcs = ["lib/transport/error_utils.cc"],
    hdrs = [
        "lib/transport/error_utils.h",
    ],
    external_deps = ["absl/status"],
    deps = [
        "error",
        "experiments",
        "http2_status",
        "status_conversion",
        "status_helper",
        "time",
        "//:gpr",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "connectivity_state",
    srcs = [
        "lib/transport/connectivity_state.cc",
    ],
    hdrs = [
        "lib/transport/connectivity_state.h",
    ],
    external_deps = [
        "absl/container:flat_hash_set",
        "absl/log",
        "absl/status",
    ],
    deps = [
        "closure",
        "error",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "xxhash_inline",
    hdrs = ["util/xxhash_inline.h"],
    external_deps = ["xxhash"],
    deps = ["//:gpr_platform"],
)

grpc_cc_library(
    name = "grpc_lb_policy_ring_hash",
    srcs = [
        "load_balancing/ring_hash/ring_hash.cc",
    ],
    hdrs = [
        "load_balancing/ring_hash/ring_hash.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:inlined_vector",
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "client_channel_internal_header",
        "closure",
        "connectivity_state",
        "delegating_helper",
        "env",
        "error",
        "grpc_lb_policy_pick_first",
        "grpc_service_config",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "ref_counted",
        "ref_counted_string",
        "resolved_address",
        "unique_type_name",
        "validation_errors",
        "xxhash_inline",
        "//:channel_arg_names",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_round_robin",
    srcs = [
        "load_balancing/round_robin/round_robin.cc",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/meta:type_traits",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "connectivity_state",
        "json",
        "lb_endpoint_list",
        "lb_policy",
        "lb_policy_factory",
        "shared_bit_gen",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "static_stride_scheduler",
    srcs = [
        "load_balancing/weighted_round_robin/static_stride_scheduler.cc",
    ],
    hdrs = [
        "load_balancing/weighted_round_robin/static_stride_scheduler.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log:check",
        "absl/types:span",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "grpc_lb_policy_weighted_round_robin",
    srcs = [
        "load_balancing/weighted_round_robin/weighted_round_robin.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/log:check",
        "absl/meta:type_traits",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "connectivity_state",
        "experiments",
        "grpc_backend_metric_data",
        "grpc_lb_policy_weighted_target",
        "json",
        "json_args",
        "json_object_loader",
        "lb_endpoint_list",
        "lb_policy",
        "lb_policy_factory",
        "metrics",
        "ref_counted",
        "resolved_address",
        "shared_bit_gen",
        "static_stride_scheduler",
        "stats_data",
        "subchannel_interface",
        "sync",
        "time",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:oob_backend_metric",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:stats",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_outlier_detection_header",
    hdrs = [
        "load_balancing/outlier_detection/outlier_detection.h",
    ],
    deps = [
        "json",
        "json_args",
        "json_object_loader",
        "time",
        "validation_errors",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_outlier_detection",
    srcs = [
        "load_balancing/outlier_detection/outlier_detection.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/log:check",
        "absl/meta:type_traits",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "connectivity_state",
        "delegating_helper",
        "experiments",
        "grpc_outlier_detection_header",
        "health_check_client",
        "iomgr_fwd",
        "json",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "ref_counted",
        "resolved_address",
        "shared_bit_gen",
        "subchannel_interface",
        "sync",
        "unique_type_name",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_trace",
        "//:lb_child_policy_handler",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_priority",
    srcs = [
        "load_balancing/priority/priority.cc",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "connectivity_state",
        "delegating_helper",
        "grpc_lb_address_filtering",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "ref_counted_string",
        "time",
        "validation_errors",
        "//:channel_arg_names",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:lb_child_policy_handler",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_weighted_target",
    srcs = [
        "load_balancing/weighted_target/weighted_target.cc",
    ],
    hdrs = [
        "load_balancing/weighted_target/weighted_target.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/log:check",
        "absl/meta:type_traits",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "connectivity_state",
        "delegating_helper",
        "grpc_lb_address_filtering",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "pollset_set",
        "shared_bit_gen",
        "sync",
        "time",
        "validation_errors",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:lb_child_policy_handler",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "grpc_lb_policy_xds_override_host",
    srcs = [
        "load_balancing/xds/xds_override_host.cc",
    ],
    hdrs = [
        "load_balancing/xds/xds_override_host.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/functional:function_ref",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "channel_args",
        "client_channel_internal_header",
        "closure",
        "connectivity_state",
        "delegating_helper",
        "error",
        "experiments",
        "grpc_stateful_session_filter",
        "iomgr_fwd",
        "json",
        "json_args",
        "json_object_loader",
        "lb_policy",
        "lb_policy_factory",
        "lb_policy_registry",
        "match",
        "pollset_set",
        "ref_counted_string",
        "resolved_address",
        "subchannel_interface",
        "sync",
        "validation_errors",
        "xds_config",
        "xds_health_status",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_trace",
        "//:lb_child_policy_handler",
        "//:orphanable",
        "//:parse_address",
        "//:ref_counted_ptr",
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
        "absl/container:inlined_vector",
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "opencensus-stats",
        "opencensus-tags",
    ],
    tags = [
        "grpc:broken-internally",
    ],
    deps = [
        "arena_promise",
        "call_finalization",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "context",
        "grpc_sockaddr",
        "latent_see",
        "metadata_batch",
        "resolved_address",
        "seq",
        "slice",
        "//:channel_arg_names",
        "//:config",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:parse_address",
        "//:promise",
        "//:transport_auth_context",
        "//:uri",
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
        "@com_google_protobuf//upb:base",
        "@com_google_protobuf//upb:mem",
        "absl/log",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "context",
        "experiments",
        "grpc_backend_metric_data",
        "grpc_backend_metric_provider",
        "latent_see",
        "map",
        "metadata_batch",
        "slice",
        "//:channel_arg_names",
        "//:config",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_trace",
        "//:xds_orca_upb",
    ],
)

grpc_cc_library(
    name = "polling_resolver",
    srcs = [
        "resolver/polling_resolver.cc",
    ],
    hdrs = [
        "resolver/polling_resolver.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "grpc_service_config",
        "iomgr_fwd",
        "time",
        "//:backoff",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_resolver",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri",
        "//:work_serializer",
    ],
)

grpc_cc_library(
    name = "service_config_helper",
    srcs = [
        "resolver/dns/event_engine/service_config_helper.cc",
    ],
    hdrs = [
        "resolver/dns/event_engine/service_config_helper.h",
    ],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "json",
        "json_args",
        "json_object_loader",
        "json_reader",
        "json_writer",
        "status_helper",
        "//:gpr_platform",
        "//:iomgr",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_event_engine",
    srcs = [
        "resolver/dns/event_engine/event_engine_client_channel_resolver.cc",
    ],
    hdrs = [
        "resolver/dns/event_engine/event_engine_client_channel_resolver.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "event_engine_common",
        "grpc_service_config",
        "polling_resolver",
        "service_config_helper",
        "sync",
        "time",
        "validation_errors",
        "//:backoff",
        "//:channel_arg_names",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_grpclb_balancer_addresses",
        "//:grpc_resolver",
        "//:grpc_service_config_impl",
        "//:grpc_trace",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_dns_plugin",
    srcs = [
        "resolver/dns/dns_resolver_plugin.cc",
    ],
    hdrs = [
        "resolver/dns/dns_resolver_plugin.h",
    ],
    external_deps = [
        "absl/log",
        "absl/strings",
    ],
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
        "resolver/dns/native/dns_resolver.cc",
    ],
    hdrs = [
        "resolver/dns/native/dns_resolver.h",
    ],
    external_deps = [
        "absl/functional:bind_front",
        "absl/log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "polling_resolver",
        "resolved_address",
        "time",
        "//:backoff",
        "//:channel_arg_names",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc_resolver",
        "//:grpc_trace",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_sockaddr",
    srcs = [
        "resolver/sockaddr/sockaddr_resolver.cc",
    ],
    external_deps = [
        "absl/log",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "channel_args",
        "iomgr_port",
        "resolved_address",
        "//:config",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc_resolver",
        "//:orphanable",
        "//:parse_address",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_xds_attributes",
    hdrs = [
        "resolver/xds/xds_resolver_attributes.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "grpc_service_config",
        "unique_type_name",
        "xds_route_config",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "context_list_entry",
    hdrs = [
        "telemetry/context_list_entry.h",
    ],
    deps = [
        "tcp_tracer",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "tcp_tracer",
    srcs = [
        "//src/core:telemetry/tcp_tracer.cc",
    ],
    hdrs = [
        "//src/core:telemetry/tcp_tracer.h",
    ],
    external_deps = [
        "absl/strings",
        "absl/time",
    ],
    visibility = ["//bazel:tcp_tracer"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "default_tcp_tracer",
    srcs = [
        "telemetry/default_tcp_tracer.cc",
    ],
    hdrs = [
        "telemetry/default_tcp_tracer.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/time",
        "absl/strings",
        "absl/types:optional",
    ],
    deps = [
        "channel_args_endpoint_config",
        "metrics",
        "sync",
        "tcp_tracer",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "xds_config",
    srcs = [
        "resolver/xds/xds_config.cc",
    ],
    hdrs = [
        "resolver/xds/xds_config.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "match",
        "ref_counted",
        "xds_cluster",
        "xds_endpoint",
        "xds_listener",
        "xds_route_config",
    ],
)

grpc_cc_library(
    name = "xds_dependency_manager",
    srcs = [
        "resolver/xds/xds_dependency_manager.cc",
    ],
    hdrs = [
        "resolver/xds/xds_dependency_manager.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/container:flat_hash_set",
        "absl/log",
        "absl/log:check",
        "absl/strings",
    ],
    deps = [
        "grpc_lb_xds_channel_args",
        "grpc_xds_client",
        "match",
        "ref_counted",
        "xds_cluster",
        "xds_config",
        "xds_endpoint",
        "xds_listener",
        "xds_route_config",
        "//:config",
        "//:grpc_resolver",
        "//:grpc_resolver_fake",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_xds",
    srcs = [
        "resolver/xds/xds_resolver.cc",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/meta:type_traits",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "re2",
    ],
    deps = [
        "arena",
        "arena_promise",
        "channel_args",
        "channel_fwd",
        "client_channel_internal_header",
        "config_selector",
        "context",
        "dual_ref_counted",
        "experiments",
        "grpc_lb_policy_ring_hash",
        "grpc_resolver_xds_attributes",
        "grpc_service_config",
        "grpc_xds_client",
        "iomgr_fwd",
        "match",
        "metadata_batch",
        "pollset_set",
        "ref_counted",
        "shared_bit_gen",
        "slice",
        "time",
        "xds_dependency_manager",
        "xds_http_filter",
        "xds_listener",
        "xds_route_config",
        "xxhash_inline",
        "//:channel_arg_names",
        "//:config",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:grpc_resolver",
        "//:grpc_service_config_impl",
        "//:grpc_trace",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri",
        "//:work_serializer",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "grpc_resolver_c2p",
    srcs = [
        "resolver/google_c2p/google_c2p_resolver.cc",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status:statusor",
        "absl/strings",
    ],
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
        "//:grpc_resolver",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:uri",
        "//:work_serializer",
        "//:xds_client",
    ],
)

grpc_cc_library(
    name = "hpack_constants",
    hdrs = [
        "ext/transport/chttp2/transport/hpack_constants.h",
    ],
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
    external_deps = ["absl/log:check"],
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "bdp_estimator",
        "experiments",
        "http2_settings",
        "memory_quota",
        "time",
        "useful",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "ping_abuse_policy",
    srcs = [
        "ext/transport/chttp2/transport/ping_abuse_policy.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/ping_abuse_policy.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "channel_args",
        "time",
        "//:channel_arg_names",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "ping_callbacks",
    srcs = [
        "ext/transport/chttp2/transport/ping_callbacks.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/ping_callbacks.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/hash",
        "absl/log:check",
        "absl/meta:type_traits",
        "absl/random:bit_gen_ref",
        "absl/random:distributions",
    ],
    deps = [
        "time",
        "//:event_engine_base_hdrs",
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "write_size_policy",
    srcs = [
        "ext/transport/chttp2/transport/write_size_policy.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/write_size_policy.h",
    ],
    external_deps = ["absl/log:check"],
    deps = [
        "time",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "ping_rate_policy",
    srcs = [
        "ext/transport/chttp2/transport/ping_rate_policy.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/ping_rate_policy.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "channel_args",
        "experiments",
        "match",
        "time",
        "//:channel_arg_names",
        "//:gpr_platform",
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
    external_deps = [
        "absl/functional:function_ref",
        "absl/log",
        "absl/strings",
    ],
    deps = [
        "http2_status",
        "json",
        "useful",
        "//:chttp2_frame",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "http2_status",
    hdrs = [
        "ext/transport/chttp2/transport/http2_status.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "time",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "internal_channel_arg_names",
    hdrs = [
        "ext/transport/chttp2/transport/internal_channel_arg_names.h",
    ],
)

grpc_cc_library(
    name = "header_assembler",
    hdrs = [
        "ext/transport/chttp2/transport/header_assembler.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
    ],
    deps = [
        "http2_status",
        "metadata_batch",
        ":slice",
        ":slice_buffer",
        "//:chttp2_frame",
        "//:gpr_platform",
        "//:hpack_parser",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "message_assembler",
    hdrs = [
        "ext/transport/chttp2/transport/message_assembler.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
    ],
    deps = [
        "arena",
        "http2_status",
        "message",
        ":slice",
        ":slice_buffer",
        "//:chttp2_frame",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "writable_stream_list",
    hdrs = [
        "ext/transport/chttp2/transport/writable_stream_list.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
    ],
    deps = [
    ],
)

grpc_cc_library(
    name = "stream_data_queue",
    hdrs = [
        "ext/transport/chttp2/transport/stream_data_queue.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
    ],
    deps = [
        "arena",
        "http2_status",
        "message",
        "metadata_batch",
    ],
)

grpc_cc_library(
    name = "http2_client_transport",
    srcs = [
        "ext/transport/chttp2/transport/http2_client_transport.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/http2_client_transport.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "1999",
        "arena",
        "call_spine",
        "channel_args",
        "check_class_size",
        "for_each",
        "grpc_promise_endpoint",
        "header_assembler",
        "http2_settings",
        "http2_status",
        "http2_transport",
        "inter_activity_mutex",
        "internal_channel_arg_names",
        "latent_see",
        "loop",
        "map",
        "message",
        "message_assembler",
        "metadata",
        "mpsc",
        "ping_promise",
        "sync",
        ":match_promise",
        ":poll",
        ":slice",
        ":slice_buffer",
        ":try_seq",
        "//:chttp2_frame",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_trace",
        "//:hpack_encoder",
        "//:hpack_parser",
        "//:promise",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "http2_server_transport",
    srcs = [
        "ext/transport/chttp2/transport/http2_server_transport.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/http2_server_transport.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "1999",
        "arena",
        "call_destination",
        "call_spine",
        "channel_args",
        "check_class_size",
        "grpc_promise_endpoint",
        "http2_settings",
        "http2_status",
        "loop",
        "map",
        "message",
        "metadata",
        "mpsc",
        "ref_counted",
        "sync",
        ":match_promise",
        ":poll",
        ":slice",
        ":slice_buffer",
        ":try_seq",
        "//:chttp2_frame",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_trace",
        "//:hpack_encoder",
        "//:hpack_parser",
        "//:promise",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "http2_transport",
    srcs = [
        "ext/transport/chttp2/transport/http2_transport.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/http2_transport.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "1999",
        "arena",
        "call_spine",
        "channel_args",
        "grpc_promise_endpoint",
        "http2_settings",
        "loop",
        "map",
        "mpsc",
        "sync",
        ":match_promise",
        ":poll",
        ":slice",
        ":slice_buffer",
        ":try_seq",
        "//:chttp2_frame",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_trace",
        "//:hpack_encoder",
        "//:hpack_parser",
        "//:promise",
        "//:ref_counted_ptr",
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
    external_deps = ["absl/log:check"],
    deps = [
        "useful",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "grpc_transport_chttp2_plugin",
    srcs = [
        "ext/transport/chttp2/chttp2_plugin.cc",
    ],
    deps = [
        "endpoint_transport",
        "grpc_transport_chttp2_client_connector",
        "grpc_transport_chttp2_server",
        "//:config",
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
    ],
    deps = [
        "channel_args",
        "channel_args_endpoint_config",
        "channel_args_preconditioning",
        "channel_stack_type",
        "closure",
        "endpoint_transport_client_channel_factory",
        "error",
        "error_utils",
        "grpc_insecure_credentials",
        "handshaker_registry",
        "resolved_address",
        "status_helper",
        "subchannel_connector",
        "sync",
        "tcp_connect_handshaker",
        "time",
        "unique_type_name",
        "//:channel",
        "//:channel_arg_names",
        "//:channel_create",
        "//:channelz",
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
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:sockaddr_utils",
        "//:transport_auth_context",
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
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "channel_args",
        "channel_args_endpoint_config",
        "closure",
        "connection_quota",
        "error",
        "error_utils",
        "event_engine_common",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "event_engine_tcp_socket_utils",
        "event_engine_utils",
        "grpc_insecure_credentials",
        "handshaker_registry",
        "iomgr_fwd",
        "match",
        "memory_quota",
        "pollset_set",
        "resolved_address",
        "resource_quota",
        "status_helper",
        "sync",
        "time",
        "unique_type_name",
        "//:channel_arg_names",
        "//:channelz",
        "//:chttp2_legacy_frame",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:grpc_transport_chttp2",
        "//:handshaker",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server",
        "//:sockaddr_utils",
        "//:transport_auth_context",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "grpc_transport_inproc",
    srcs = [
        "ext/transport/inproc/inproc_transport.cc",
        "ext/transport/inproc/legacy_inproc_transport.cc",
    ],
    hdrs = [
        "ext/transport/inproc/inproc_transport.h",
        "ext/transport/inproc/legacy_inproc_transport.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "arena",
        "channel_args",
        "channel_args_preconditioning",
        "channel_stack_type",
        "closure",
        "connectivity_state",
        "error",
        "event_engine_context",
        "experiments",
        "iomgr_fwd",
        "metadata",
        "metadata_batch",
        "resource_quota",
        "slice",
        "slice_buffer",
        "status_helper",
        "time",
        "try_seq",
        "//:channel",
        "//:channel_arg_names",
        "//:channel_create",
        "//:channelz",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
        "//:iomgr",
        "//:promise",
        "//:ref_counted_ptr",
        "//:server",
    ],
)

grpc_internal_proto_library(
    name = "chaotic_good_frame_proto",
    srcs = ["ext/transport/chaotic_good/chaotic_good_frame.proto"],
    has_services = False,
)

grpc_cc_proto_library(
    name = "chaotic_good_frame_cc_proto",
    deps = ["chaotic_good_frame_proto"],
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
        "absl/log:check",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "arena",
        "bitset",
        "chaotic_good_frame_cc_proto",
        "chaotic_good_frame_header",
        "context",
        "match",
        "message",
        "metadata",
        "no_destruct",
        "slice",
        "slice_buffer",
        "status_flag",
        "status_helper",
        "switch",
        "//:gpr_platform",
        "//:grpc_base",
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
        "absl/strings",
    ],
    deps = [
        "bitset",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "chaotic_good",
    srcs = [
        "ext/transport/chaotic_good/chaotic_good.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/chaotic_good.h",
    ],
    external_deps = [
        "absl/strings",
    ],
    visibility = ["//bazel:chaotic_good"],
    deps = [
        "chaotic_good_connector",
        "chaotic_good_server",
        "//:config",
    ],
)

grpc_cc_library(
    name = "chaotic_good_config",
    hdrs = [
        "ext/transport/chaotic_good/config.h",
    ],
    external_deps = ["absl/container:flat_hash_set"],
    deps = [
        "channel_args",
        "chaotic_good_frame_cc_proto",
        "chaotic_good_message_chunker",
        "chaotic_good_pending_connection",
        "chaotic_good_tcp_frame_transport",
        "event_engine_extensions",
    ],
)

grpc_cc_library(
    name = "chaotic_good_tcp_frame_header",
    srcs = [
        "ext/transport/chaotic_good/tcp_frame_header.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/tcp_frame_header.h",
    ],
    external_deps = [
        "absl/strings",
    ],
    deps = [
        "chaotic_good_frame_header",
        "chaotic_good_serialize_little_endian",
    ],
)

grpc_cc_library(
    name = "chaotic_good_tcp_ztrace_collector",
    srcs = [
        "ext/transport/chaotic_good/tcp_ztrace_collector.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/tcp_ztrace_collector.h",
    ],
    deps = [
        "chaotic_good_frame_header",
        "chaotic_good_tcp_frame_header",
        "event_engine_utils",
        "ztrace_collector",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "chaotic_good_message_chunker",
    hdrs = [
        "ext/transport/chaotic_good/message_chunker.h",
    ],
    deps = [
        "chaotic_good_frame",
        "chaotic_good_frame_transport",
        "if",
        "loop",
        "map",
        "seq",
        "status_flag",
    ],
)

grpc_cc_library(
    name = "chaotic_good_message_reassembly",
    hdrs = [
        "ext/transport/chaotic_good/message_reassembly.h",
    ],
    external_deps = ["absl/log"],
    deps = [
        "call_spine",
        "chaotic_good_frame",
    ],
)

grpc_cc_library(
    name = "chaotic_good_control_endpoint",
    srcs = [
        "ext/transport/chaotic_good/control_endpoint.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/control_endpoint.h",
    ],
    external_deps = ["absl/cleanup"],
    deps = [
        "1999",
        "chaotic_good_tcp_ztrace_collector",
        "chaotic_good_transport_context",
        "event_engine_context",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "event_engine_tcp_socket_utils",
        "grpc_promise_endpoint",
        "loop",
        "sync",
        "try_seq",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "chaotic_good_pending_connection",
    hdrs = [
        "ext/transport/chaotic_good/pending_connection.h",
    ],
    external_deps = ["absl/status:statusor"],
    deps = [
        "dual_ref_counted",
        "grpc_promise_endpoint",
        "//:promise",
    ],
)

grpc_cc_library(
    name = "chaotic_good_data_endpoints",
    srcs = [
        "ext/transport/chaotic_good/data_endpoints.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/data_endpoints.h",
    ],
    external_deps = [
        "absl/cleanup",
        "absl/strings",
    ],
    deps = [
        "1999",
        "chaotic_good_pending_connection",
        "chaotic_good_serialize_little_endian",
        "chaotic_good_tcp_frame_header",
        "chaotic_good_tcp_ztrace_collector",
        "chaotic_good_transport_context",
        "default_tcp_tracer",
        "dump_args",
        "event_engine_context",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "event_engine_tcp_socket_utils",
        "grpc_promise_endpoint",
        "loop",
        "map",
        "metrics",
        "seq",
        "seq_bit_set",
        "slice_buffer",
        "status_flag",
        "time",
        "transport_framing_endpoint_extension",
        "try_seq",
        "//:channelz",
        "//:gpr",
        "//:promise",
    ],
)

grpc_cc_library(
    name = "chaotic_good_frame_transport",
    hdrs = [
        "ext/transport/chaotic_good/frame_transport.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "1999",
        "chaotic_good_frame",
        "chaotic_good_frame_header",
        "chaotic_good_transport_context",
        "event_engine_context",
        "event_engine_tcp_socket_utils",
        "grpc_promise_endpoint",
        "inter_activity_pipe",
        "loop",
        "map",
        "match_promise",
        "mpsc",
        "pipe",
        "tcp_tracer",
        "//:promise",
    ],
)

grpc_cc_library(
    name = "chaotic_good_serialize_little_endian",
    hdrs = [
        "ext/transport/chaotic_good/serialize_little_endian.h",
    ],
)

grpc_cc_library(
    name = "chaotic_good_tcp_frame_transport",
    srcs = [
        "ext/transport/chaotic_good/tcp_frame_transport.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/tcp_frame_transport.h",
    ],
    deps = [
        "chaotic_good_control_endpoint",
        "chaotic_good_data_endpoints",
        "chaotic_good_frame_header",
        "chaotic_good_frame_transport",
        "chaotic_good_pending_connection",
        "chaotic_good_serialize_little_endian",
        "chaotic_good_tcp_frame_header",
        "chaotic_good_tcp_ztrace_collector",
        "chaotic_good_transport_context",
        "event_engine_tcp_socket_utils",
        "if",
        "inter_activity_latch",
        "join",
        "loop",
        "race",
        "seq",
        "transport_framing_endpoint_extension",
        "try_seq",
    ],
)

grpc_cc_library(
    name = "chaotic_good_transport_context",
    hdrs = [
        "ext/transport/chaotic_good/transport_context.h",
    ],
    deps = [
        "channel_args",
        "metrics",
        "ref_counted",
        "//:channelz",
        "//:event_engine_base_hdrs",
        "//:ref_counted_ptr",
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
        "absl/container:flat_hash_map",
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "activity",
        "arena",
        "chaotic_good_config",
        "chaotic_good_frame",
        "chaotic_good_frame_header",
        "chaotic_good_frame_transport",
        "chaotic_good_message_reassembly",
        "chaotic_good_pending_connection",
        "chaotic_good_transport_context",
        "context",
        "event_engine_context",
        "event_engine_query_extensions",
        "for_each",
        "grpc_promise_endpoint",
        "if",
        "inter_activity_pipe",
        "loop",
        "map",
        "memory_quota",
        "metadata_batch",
        "mpsc",
        "pipe",
        "poll",
        "resource_quota",
        "slice",
        "slice_buffer",
        "switch",
        "sync",
        "try_join",
        "try_seq",
        "//:exec_ctx",
        "//:gpr_platform",
        "//:grpc_base",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "chaotic_good_server_transport",
    srcs = [
        "ext/transport/chaotic_good/server_transport.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/server_transport.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "1999",
        "activity",
        "arena",
        "chaotic_good_config",
        "chaotic_good_frame",
        "chaotic_good_frame_header",
        "chaotic_good_frame_transport",
        "chaotic_good_message_chunker",
        "chaotic_good_message_reassembly",
        "chaotic_good_pending_connection",
        "chaotic_good_transport_context",
        "context",
        "default_event_engine",
        "event_engine_context",
        "event_engine_wakeup_scheduler",
        "for_each",
        "grpc_promise_endpoint",
        "if",
        "inter_activity_latch",
        "inter_activity_pipe",
        "loop",
        "memory_quota",
        "metadata_batch",
        "mpsc",
        "pipe",
        "poll",
        "resource_quota",
        "seq",
        "slice",
        "slice_buffer",
        "switch",
        "sync",
        "try_join",
        "try_seq",
        "//:exec_ctx",
        "//:gpr_platform",
        "//:grpc_base",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "chaotic_good_server",
    srcs = [
        "ext/transport/chaotic_good/server/chaotic_good_server.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/server/chaotic_good_server.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "activity",
        "arena",
        "channel_args",
        "channel_args_endpoint_config",
        "chaotic_good_config",
        "chaotic_good_frame",
        "chaotic_good_frame_header",
        "chaotic_good_legacy_server",
        "chaotic_good_pending_connection",
        "chaotic_good_server_transport",
        "chaotic_good_tcp_frame_transport",
        "closure",
        "context",
        "error",
        "error_utils",
        "event_engine_common",
        "event_engine_context",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "event_engine_tcp_socket_utils",
        "event_engine_utils",
        "event_engine_wakeup_scheduler",
        "grpc_promise_endpoint",
        "if",
        "inter_activity_latch",
        "iomgr_fwd",
        "join",
        "latch",
        "memory_quota",
        "metadata",
        "metadata_batch",
        "metrics",
        "race",
        "resource_quota",
        "shared_bit_gen",
        "sleep",
        "slice",
        "slice_buffer",
        "status_helper",
        "sync",
        "time",
        "try_seq",
        "//:channelz",
        "//:config",
        "//:gpr_platform",
        "//:grpc_base",
        "//:handshaker",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server",
    ],
)

grpc_cc_library(
    name = "chaotic_good_connector",
    srcs = [
        "ext/transport/chaotic_good/client/chaotic_good_connector.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good/client/chaotic_good_connector.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "activity",
        "all_ok",
        "arena",
        "channel_args",
        "channel_args_endpoint_config",
        "chaotic_good_client_transport",
        "chaotic_good_config",
        "chaotic_good_frame",
        "chaotic_good_frame_cc_proto",
        "chaotic_good_frame_header",
        "chaotic_good_legacy_connector",
        "chaotic_good_tcp_frame_transport",
        "closure",
        "context",
        "endpoint_transport_client_channel_factory",
        "error",
        "error_utils",
        "event_engine_context",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "event_engine_tcp_socket_utils",
        "event_engine_wakeup_scheduler",
        "grpc_promise_endpoint",
        "inter_activity_latch",
        "latch",
        "memory_quota",
        "metrics",
        "no_destruct",
        "notification",
        "race",
        "resource_quota",
        "sleep",
        "slice",
        "slice_buffer",
        "subchannel_connector",
        "sync",
        "tcp_connect_handshaker",
        "time",
        "try_seq",
        "wait_for_callback",
        "//:channel",
        "//:channel_create",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:handshaker",
        "//:iomgr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_frame",
    srcs = [
        "ext/transport/chaotic_good_legacy/frame.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good_legacy/frame.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "arena",
        "bitset",
        "chaotic_good_frame_cc_proto",
        "chaotic_good_legacy_frame_header",
        "context",
        "match",
        "message",
        "metadata",
        "no_destruct",
        "slice",
        "slice_buffer",
        "status_helper",
        "//:gpr_platform",
        "//:grpc_base",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_frame_header",
    srcs = [
        "ext/transport/chaotic_good_legacy/frame_header.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good_legacy/frame_header.h",
    ],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "bitset",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_config",
    hdrs = [
        "ext/transport/chaotic_good_legacy/config.h",
    ],
    external_deps = ["absl/container:flat_hash_set"],
    deps = [
        "channel_args",
        "chaotic_good_frame_cc_proto",
        "chaotic_good_legacy_message_chunker",
        "chaotic_good_legacy_pending_connection",
        "chaotic_good_legacy_transport",
        "event_engine_extensions",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_message_chunker",
    hdrs = [
        "ext/transport/chaotic_good_legacy/message_chunker.h",
    ],
    deps = [
        "chaotic_good_legacy_frame",
        "if",
        "loop",
        "map",
        "seq",
        "status_flag",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_message_reassembly",
    hdrs = [
        "ext/transport/chaotic_good_legacy/message_reassembly.h",
    ],
    external_deps = ["absl/log"],
    deps = [
        "call_spine",
        "chaotic_good_legacy_frame",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_control_endpoint",
    srcs = [
        "ext/transport/chaotic_good_legacy/control_endpoint.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good_legacy/control_endpoint.h",
    ],
    external_deps = ["absl/cleanup"],
    deps = [
        "1999",
        "event_engine_context",
        "event_engine_tcp_socket_utils",
        "grpc_promise_endpoint",
        "loop",
        "sync",
        "try_seq",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_pending_connection",
    hdrs = [
        "ext/transport/chaotic_good_legacy/pending_connection.h",
    ],
    external_deps = ["absl/status:statusor"],
    deps = [
        "dual_ref_counted",
        "grpc_promise_endpoint",
        "//:promise",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_data_endpoints",
    srcs = [
        "ext/transport/chaotic_good_legacy/data_endpoints.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good_legacy/data_endpoints.h",
    ],
    external_deps = [
        "absl/cleanup",
        "absl/strings",
    ],
    deps = [
        "1999",
        "chaotic_good_legacy_pending_connection",
        "default_tcp_tracer",
        "event_engine_context",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "event_engine_tcp_socket_utils",
        "grpc_promise_endpoint",
        "loop",
        "metrics",
        "seq",
        "slice_buffer",
        "try_seq",
        "//:promise",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_transport",
    hdrs = [
        "ext/transport/chaotic_good_legacy/chaotic_good_transport.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "call_spine",
        "chaotic_good_legacy_control_endpoint",
        "chaotic_good_legacy_data_endpoints",
        "chaotic_good_legacy_frame",
        "chaotic_good_legacy_frame_header",
        "event_engine_context",
        "event_engine_tcp_socket_utils",
        "grpc_promise_endpoint",
        "loop",
        "match_promise",
        "mpsc",
        "seq",
        "try_join",
        "try_seq",
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_client_transport",
    srcs = [
        "ext/transport/chaotic_good_legacy/client_transport.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good_legacy/client_transport.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/container:flat_hash_map",
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "activity",
        "arena",
        "chaotic_good_legacy_config",
        "chaotic_good_legacy_frame",
        "chaotic_good_legacy_frame_header",
        "chaotic_good_legacy_message_reassembly",
        "chaotic_good_legacy_pending_connection",
        "chaotic_good_legacy_transport",
        "context",
        "event_engine_context",
        "event_engine_query_extensions",
        "for_each",
        "grpc_promise_endpoint",
        "if",
        "inter_activity_pipe",
        "loop",
        "map",
        "memory_quota",
        "metadata_batch",
        "metrics",
        "mpsc",
        "pipe",
        "poll",
        "resource_quota",
        "slice",
        "slice_buffer",
        "switch",
        "sync",
        "try_join",
        "try_seq",
        "//:exec_ctx",
        "//:gpr_platform",
        "//:grpc_base",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_server_transport",
    srcs = [
        "ext/transport/chaotic_good_legacy/server_transport.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good_legacy/server_transport.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/cleanup",
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "1999",
        "activity",
        "arena",
        "chaotic_good_legacy_config",
        "chaotic_good_legacy_frame",
        "chaotic_good_legacy_frame_header",
        "chaotic_good_legacy_message_reassembly",
        "chaotic_good_legacy_pending_connection",
        "chaotic_good_legacy_transport",
        "context",
        "default_event_engine",
        "event_engine_context",
        "event_engine_wakeup_scheduler",
        "for_each",
        "grpc_promise_endpoint",
        "if",
        "inter_activity_latch",
        "inter_activity_pipe",
        "loop",
        "memory_quota",
        "metadata_batch",
        "metrics",
        "mpsc",
        "pipe",
        "poll",
        "resource_quota",
        "seq",
        "slice",
        "slice_buffer",
        "switch",
        "sync",
        "try_join",
        "try_seq",
        "//:exec_ctx",
        "//:gpr_platform",
        "//:grpc_base",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_server",
    srcs = [
        "ext/transport/chaotic_good_legacy/server/chaotic_good_server.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good_legacy/server/chaotic_good_server.h",
    ],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "activity",
        "arena",
        "channel_args",
        "channel_args_endpoint_config",
        "chaotic_good_legacy_config",
        "chaotic_good_legacy_frame",
        "chaotic_good_legacy_frame_header",
        "chaotic_good_legacy_pending_connection",
        "chaotic_good_legacy_server_transport",
        "closure",
        "context",
        "error",
        "error_utils",
        "event_engine_common",
        "event_engine_context",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "event_engine_tcp_socket_utils",
        "event_engine_utils",
        "event_engine_wakeup_scheduler",
        "grpc_promise_endpoint",
        "if",
        "inter_activity_latch",
        "iomgr_fwd",
        "join",
        "latch",
        "memory_quota",
        "metadata",
        "metadata_batch",
        "race",
        "resource_quota",
        "shared_bit_gen",
        "sleep",
        "slice",
        "slice_buffer",
        "status_helper",
        "sync",
        "time",
        "try_seq",
        "//:channelz",
        "//:config",
        "//:gpr_platform",
        "//:grpc_base",
        "//:handshaker",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:server",
    ],
)

grpc_cc_library(
    name = "chaotic_good_legacy_connector",
    srcs = [
        "ext/transport/chaotic_good_legacy/client/chaotic_good_connector.cc",
    ],
    hdrs = [
        "ext/transport/chaotic_good_legacy/client/chaotic_good_connector.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/random",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/status:statusor",
    ],
    deps = [
        "activity",
        "all_ok",
        "arena",
        "channel_args",
        "channel_args_endpoint_config",
        "chaotic_good_frame_cc_proto",
        "chaotic_good_legacy_client_transport",
        "chaotic_good_legacy_config",
        "chaotic_good_legacy_frame",
        "chaotic_good_legacy_frame_header",
        "closure",
        "context",
        "error",
        "error_utils",
        "event_engine_context",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "event_engine_tcp_socket_utils",
        "event_engine_wakeup_scheduler",
        "grpc_promise_endpoint",
        "inter_activity_latch",
        "latch",
        "memory_quota",
        "no_destruct",
        "notification",
        "race",
        "resource_quota",
        "sleep",
        "slice",
        "slice_buffer",
        "subchannel_connector",
        "sync",
        "time",
        "try_seq",
        "wait_for_callback",
        "//:channel",
        "//:channel_create",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:handshaker",
        "//:iomgr",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "gcp_metadata_query",
    srcs = [
        "util/gcp_metadata_query.cc",
    ],
    hdrs = [
        "util/gcp_metadata_query.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log",
        "absl/log:check",
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
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//:grpc_trace",
        "//:httpcli",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "check_class_size",
    hdrs = [
        "util/check_class_size.h",
    ],
    deps = [
        "//:gpr_platform",
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
        "absl/log",
        "absl/numeric:int128",
        "absl/random",
        "absl/random:distributions",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "arena",
        "arena_promise",
        "cancel_callback",
        "channel_args",
        "channel_fwd",
        "channel_stack_type",
        "context",
        "latent_see",
        "logging_sink",
        "map",
        "metadata_batch",
        "pipe",
        "slice",
        "slice_buffer",
        "time",
        "//:call_tracer",
        "//:channel_arg_names",
        "//:config",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_base",
        "//:grpc_client_channel",
        "//:grpc_public_hdrs",
        "//:grpc_resolver",
        "//:uri",
    ],
)

grpc_cc_library(
    name = "grpc_promise_endpoint",
    srcs = [
        "lib/transport/promise_endpoint.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
    ],
    public_hdrs = [
        "lib/transport/promise_endpoint.h",
    ],
    deps = [
        "activity",
        "cancel_callback",
        "dump_args",
        "event_engine_common",
        "event_engine_extensions",
        "event_engine_query_extensions",
        "if",
        "map",
        "poll",
        "slice",
        "slice_buffer",
        "sync",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "call_final_info",
    srcs = [
        "lib/transport/call_final_info.cc",
    ],
    hdrs = [
        "lib/transport/call_final_info.h",
    ],
    deps = [
        "//:gpr",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "function_signature",
    hdrs = [
        "util/function_signature.h",
    ],
    external_deps = [
        "absl/strings",
    ],
)

grpc_cc_library(
    name = "call_finalization",
    hdrs = [
        "call/call_finalization.h",
    ],
    visibility = ["//bazel:alt_grpc_base_legacy"],
    deps = [
        "arena",
        "call_final_info",
        "context",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "call_state",
    srcs = [
        "call/call_state.cc",
    ],
    hdrs = [
        "call/call_state.h",
    ],
    deps = [
        "activity",
        "poll",
        "status_flag",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "call_filters",
    srcs = [
        "call/call_filters.cc",
    ],
    hdrs = [
        "call/call_filters.h",
    ],
    external_deps = [
        "absl/log",
        "absl/log:check",
    ],
    deps = [
        "call_final_info",
        "call_state",
        "dump_args",
        "for_each",
        "if",
        "latch",
        "map",
        "message",
        "metadata",
        "ref_counted",
        "seq",
        "status_flag",
        "try_seq",
        "//:gpr",
        "//:promise",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "filter_fusion",
    hdrs = [
        "call/filter_fusion.h",
    ],
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/log:check",
        "absl/log",
        "absl/memory",
    ],
    deps = [
        "call_filters",
        "call_final_info",
        "channel_args",
        "metadata",
        "status_helper",
        "type_list",
        "//:grpc_base",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "interception_chain",
    srcs = [
        "call/interception_chain.cc",
    ],
    hdrs = [
        "call/interception_chain.h",
    ],
    deps = [
        "call_destination",
        "call_filters",
        "call_spine",
        "match",
        "metadata",
        "ref_counted",
        "//:gpr_platform",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "call_destination",
    hdrs = [
        "call/call_destination.h",
    ],
    deps = [
        "call_spine",
        "//:gpr_platform",
        "//:orphanable",
    ],
)

grpc_cc_library(
    name = "parsed_metadata",
    srcs = [
        "call/parsed_metadata.cc",
    ],
    hdrs = [
        "call/parsed_metadata.h",
    ],
    external_deps = [
        "absl/functional:function_ref",
        "absl/meta:type_traits",
        "absl/strings",
    ],
    deps = [
        "slice",
        "time",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "metadata",
    srcs = [
        "call/metadata.cc",
    ],
    hdrs = [
        "call/metadata.h",
    ],
    deps = [
        "error_utils",
        "metadata_batch",
        "try_seq",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "message",
    srcs = [
        "call/message.cc",
    ],
    hdrs = [
        "call/message.h",
    ],
    external_deps = ["absl/strings"],
    deps = [
        "arena",
        "slice_buffer",
        "//:gpr_platform",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "call_spine",
    srcs = [
        "call/call_spine.cc",
    ],
    hdrs = [
        "call/call_spine.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log",
        "absl/log:check",
    ],
    deps = [
        "1999",
        "call_arena_allocator",
        "call_filters",
        "dual_ref_counted",
        "event_engine_context",
        "for_each",
        "if",
        "latch",
        "message",
        "metadata",
        "pipe",
        "prioritized_race",
        "promise_status",
        "status_flag",
        "try_seq",
        "//:gpr",
        "//:promise",
    ],
)

grpc_cc_library(
    name = "direct_channel",
    srcs = [
        "client_channel/direct_channel.cc",
    ],
    hdrs = [
        "client_channel/direct_channel.h",
    ],
    deps = [
        "channel_stack_type",
        "event_engine_context",
        "interception_chain",
        "//:channel",
        "//:config",
        "//:grpc_base",
        "//:orphanable",
    ],
)

grpc_cc_library(
    name = "metadata_batch",
    srcs = [
        "call/metadata_batch.cc",
    ],
    hdrs = [
        "call/custom_metadata.h",
        "call/metadata_batch.h",
        "call/simple_slice_based_metadata.h",
    ],
    external_deps = [
        "absl/base:no_destructor",
        "absl/container:flat_hash_set",
        "absl/container:inlined_vector",
        "absl/functional:function_ref",
        "absl/log",
        "absl/log:check",
        "absl/meta:type_traits",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "chunked_vector",
        "compression",
        "experiments",
        "if_list",
        "metadata_compression_traits",
        "packed_table",
        "parsed_metadata",
        "poll",
        "slice",
        "time",
        "timeout_encoding",
        "type_list",
        "//:gpr",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "endpoint_transport",
    hdrs = [
        "transport/endpoint_transport.h",
    ],
    external_deps = [
        "absl/strings",
    ],
    visibility = ["//bazel:chaotic_good"],
    deps = [
        "channel_args",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_library(
    name = "endpoint_transport_client_channel_factory",
    srcs = [
        "transport/endpoint_transport_client_channel_factory.cc",
    ],
    hdrs = [
        "transport/endpoint_transport_client_channel_factory.h",
    ],
    deps = [
        "channel_args",
        "no_destruct",
        "//:grpc_client_channel",
        "//:grpc_security_base",
    ],
)

grpc_cc_library(
    name = "timeout_encoding",
    srcs = [
        "lib/transport/timeout_encoding.cc",
    ],
    hdrs = [
        "lib/transport/timeout_encoding.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:check",
    ],
    deps = [
        "slice",
        "time",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "call_arena_allocator",
    srcs = [
        "call/call_arena_allocator.cc",
    ],
    hdrs = [
        "call/call_arena_allocator.h",
    ],
    deps = [
        "arena",
        "memory_quota",
        "ref_counted",
        "//:gpr_platform",
    ],
)

grpc_cc_library(
    name = "compression",
    srcs = [
        "lib/compression/compression.cc",
        "lib/compression/compression_internal.cc",
    ],
    hdrs = [
        "lib/compression/compression_internal.h",
    ],
    external_deps = [
        "absl/container:inlined_vector",
        "absl/log:check",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        "bitset",
        "channel_args",
        "ref_counted_string",
        "slice",
        "useful",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
        "//:ref_counted_ptr",
    ],
)

grpc_cc_library(
    name = "metrics",
    srcs = [
        "telemetry/metrics.cc",
    ],
    hdrs = [
        "telemetry/metrics.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/functional:function_ref",
        "absl/log:check",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "channel_args",
        "no_destruct",
        "slice",
        "sync",
        "time",
        "//:call_tracer",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "wait_for_single_owner",
    hdrs = ["util/wait_for_single_owner.h"],
    external_deps = ["absl/log"],
    deps = [
        "time",
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "ping_promise",
    srcs = [
        "ext/transport/chttp2/transport/ping_promise.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/ping_promise.h",
    ],
    external_deps = [
        "absl/log",
        "absl/status",
    ],
    deps = [
        "1999",
        "if",
        "inter_activity_latch",
        "latch",
        "map",
        "match",
        "ping_abuse_policy",
        "ping_callbacks",
        "ping_rate_policy",
        "race",
        "shared_bit_gen",
        "sleep",
        "time",
        "try_seq",
        "//:promise",
    ],
)

grpc_cc_library(
    name = "keepalive",
    srcs = [
        "ext/transport/chttp2/transport/keepalive.cc",
    ],
    hdrs = [
        "ext/transport/chttp2/transport/keepalive.h",
    ],
    external_deps = [
        "absl/log",
        "absl/status",
    ],
    deps = [
        "1999",
        "all_ok",
        "if",
        "latch",
        "loop",
        "race",
        "sleep",
        "try_seq",
        "//:promise",
    ],
)

grpc_cc_library(
    name = "spiffe_utils",
    srcs = [
        "//src/core:credentials/transport/tls/spiffe_utils.cc",
    ],
    hdrs = [
        "//src/core:credentials/transport/tls/spiffe_utils.h",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "libcrypto",
        "libssl",
    ],
    deps = [
        "json",
        "json_args",
        "json_object_loader",
        "json_reader",
        "load_file",
        "status_helper",
        "//:tsi_ssl_credentials",
    ],
)

grpc_cc_library(
    name = "ztrace_collector",
    hdrs = ["channelz/ztrace_collector.h"],
    defines = select({
        "//:grpc_no_ztrace": ["GRPC_NO_ZTRACE"],
        "//conditions:default": [],
    }),
    external_deps = [
        "absl/container:flat_hash_set",
    ],
    deps = [
        "json_writer",
        "single_set_ptr",
        "sync",
        "time",
        "//:channelz",
        "//:gpr",
        "//:grpc_trace",
    ],
)

grpc_cc_library(
    name = "http2_ztrace_collector",
    hdrs = ["ext/transport/chttp2/transport/http2_ztrace_collector.h"],
    external_deps = [
        "absl/container:flat_hash_set",
    ],
    deps = [
        "ztrace_collector",
        "//:chttp2_frame",
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

grpc_upb_proto_reflection_library(
    name = "envoy_config_core_upbdefs",
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
    name = "envoy_extensions_filters_http_gcp_authn_upb",
    deps = ["@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_http_gcp_authn_upbdefs",
    deps = ["@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg"],
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
    deps = [
        "@envoy_api//envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3:pkg",
    ],
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
    deps = [
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg",
    ],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_filters_network_http_connection_manager_upbdefs",
    deps = [
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg",
    ],
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
    name = "envoy_extensions_transport_sockets_http_11_proxy_upb",
    deps = ["@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_transport_sockets_http_11_proxy_upbdefs",
    deps = ["@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg"],
)

grpc_upb_proto_library(
    name = "envoy_extensions_upstreams_http_upb",
    deps = ["@envoy_api//envoy/extensions/upstreams/http/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "envoy_extensions_upstreams_http_upbdefs",
    deps = ["@envoy_api//envoy/extensions/upstreams/http/v3:pkg"],
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
    deps = ["@com_github_cncf_xds//xds/type/v3:pkg"],
)

grpc_upb_proto_reflection_library(
    name = "xds_type_upbdefs",
    deps = ["@com_github_cncf_xds//xds/type/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_orca_upb",
    deps = ["@com_github_cncf_xds//xds/data/orca/v3:pkg"],
)

grpc_upb_proto_library(
    name = "xds_orca_service_upb",
    deps = ["@com_github_cncf_xds//xds/service/orca/v3:pkg"],
)

grpc_upb_proto_library(
    name = "grpc_health_upb",
    deps = ["//src/proto/grpc/health/v1:health_proto"],
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
    deps = ["//src/proto/grpc/lb/v1:load_balancer_proto"],
)

grpc_upb_proto_library(
    name = "alts_upb",
    deps = ["//src/proto/grpc/gcp:alts_handshaker_proto"],
)

grpc_upb_proto_library(
    name = "rls_upb",
    deps = ["//src/proto/grpc/lookup/v1:rls_proto"],
)

grpc_upb_proto_library(
    name = "rls_config_upb",
    deps = ["//src/proto/grpc/lookup/v1:rls_config_proto"],
)

grpc_upb_proto_reflection_library(
    name = "rls_config_upbdefs",
    deps = ["//src/proto/grpc/lookup/v1:rls_config_proto"],
)

WELL_KNOWN_PROTO_TARGETS = [
    "any",
    "duration",
    "empty",
    "struct",
    "timestamp",
    "wrappers",
]

[
    grpc_upb_proto_library(
        name = "protobuf_" + target + "_upb",
        deps = ["@com_google_protobuf//:" + target + "_proto"],
    )
    for target in WELL_KNOWN_PROTO_TARGETS
]

[
    grpc_upb_proto_reflection_library(
        name = "protobuf_" + target + "_upbdefs",
        deps = ["@com_google_protobuf//:" + target + "_proto"],
    )
    for target in WELL_KNOWN_PROTO_TARGETS
]

grpc_generate_one_off_internal_targets()
