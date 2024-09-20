// Copyright 2023 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Auto generated by tools/codegen/core/gen_experiments.py

#include "src/core/lib/experiments/experiments.h"

#include <grpc/support/port_platform.h>

#ifndef GRPC_EXPERIMENTS_ARE_FINAL

#if defined(GRPC_CFSTREAM)
namespace {
const char* const description_call_tracer_in_transport =
    "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const char* const description_canary_client_privacy =
    "If set, canary client privacy";
const char* const additional_constraints_canary_client_privacy = "{}";
const char* const description_client_privacy = "If set, client privacy";
const char* const additional_constraints_client_privacy = "{}";
const char* const description_event_engine_application_callbacks =
    "Run application callbacks in EventEngine threads, instead of on the "
    "thread-local ApplicationCallbackExecCtx";
const char* const additional_constraints_event_engine_application_callbacks =
    "{}";
const char* const description_event_engine_client =
    "Use EventEngine clients instead of iomgr's grpc_tcp_client";
const char* const additional_constraints_event_engine_client = "{}";
const char* const description_event_engine_dns =
    "If set, use EventEngine DNSResolver for client channel resolution";
const char* const additional_constraints_event_engine_dns = "{}";
const char* const description_event_engine_listener =
    "Use EventEngine listeners instead of iomgr's grpc_tcp_server";
const char* const additional_constraints_event_engine_listener = "{}";
const char* const description_free_large_allocator =
    "If set, return all free bytes from a \042big\042 allocator";
const char* const additional_constraints_free_large_allocator = "{}";
const char* const description_max_pings_wo_data_throttle =
    "Experiment to throttle pings to a period of 1 min when "
    "GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA limit has reached (instead of "
    "completely blocking).";
const char* const additional_constraints_max_pings_wo_data_throttle = "{}";
const char* const description_monitoring_experiment =
    "Placeholder experiment to prove/disprove our monitoring is working";
const char* const additional_constraints_monitoring_experiment = "{}";
const char* const description_multiping =
    "Allow more than one ping to be in flight at a time by default.";
const char* const additional_constraints_multiping = "{}";
const char* const description_pick_first_new =
    "New pick_first impl with memory reduction.";
const char* const additional_constraints_pick_first_new = "{}";
const char* const description_promise_based_inproc_transport =
    "Use promises for the in-process transport.";
const char* const additional_constraints_promise_based_inproc_transport = "{}";
const char* const description_schedule_cancellation_over_write =
    "Allow cancellation op to be scheduled over a write";
const char* const additional_constraints_schedule_cancellation_over_write =
    "{}";
const char* const description_server_privacy = "If set, server privacy";
const char* const additional_constraints_server_privacy = "{}";
const char* const description_tcp_frame_size_tuning =
    "If set, enables TCP to use RPC size estimation made by higher layers. TCP "
    "would not indicate completion of a read operation until a specified "
    "number of bytes have been read over the socket. Buffers are also "
    "allocated according to estimated RPC sizes.";
const char* const additional_constraints_tcp_frame_size_tuning = "{}";
const char* const description_tcp_rcv_lowat =
    "Use SO_RCVLOWAT to avoid wakeups on the read path.";
const char* const additional_constraints_tcp_rcv_lowat = "{}";
const char* const description_time_caching_in_party =
    "Disable time caching in exec_ctx, and enable it only in a single party "
    "execution.";
const char* const additional_constraints_time_caching_in_party = "{}";
const char* const description_trace_record_callops =
    "Enables tracing of call batch initiation and completion.";
const char* const additional_constraints_trace_record_callops = "{}";
const char* const description_unconstrained_max_quota_buffer_size =
    "Discard the cap on the max free pool size for one memory allocator";
const char* const additional_constraints_unconstrained_max_quota_buffer_size =
    "{}";
const char* const description_work_serializer_clears_time_cache =
    "Have the work serializer clear the time cache when it dispatches work.";
const char* const additional_constraints_work_serializer_clears_time_cache =
    "{}";
const char* const description_work_serializer_dispatch =
    "Have the work serializer dispatch work to event engine for every "
    "callback, instead of running things inline in the first thread that "
    "successfully enqueues work.";
const char* const additional_constraints_work_serializer_dispatch = "{}";
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
    {"call_tracer_in_transport", description_call_tracer_in_transport,
     additional_constraints_call_tracer_in_transport, nullptr, 0, true, true},
    {"canary_client_privacy", description_canary_client_privacy,
     additional_constraints_canary_client_privacy, nullptr, 0, false, false},
    {"client_privacy", description_client_privacy,
     additional_constraints_client_privacy, nullptr, 0, false, false},
    {"event_engine_application_callbacks",
     description_event_engine_application_callbacks,
     additional_constraints_event_engine_application_callbacks, nullptr, 0,
     true, true},
    {"event_engine_client", description_event_engine_client,
     additional_constraints_event_engine_client, nullptr, 0, false, true},
    {"event_engine_dns", description_event_engine_dns,
     additional_constraints_event_engine_dns, nullptr, 0, false, false},
    {"event_engine_listener", description_event_engine_listener,
     additional_constraints_event_engine_listener, nullptr, 0, false, true},
    {"free_large_allocator", description_free_large_allocator,
     additional_constraints_free_large_allocator, nullptr, 0, false, true},
    {"max_pings_wo_data_throttle", description_max_pings_wo_data_throttle,
     additional_constraints_max_pings_wo_data_throttle, nullptr, 0, false,
     true},
    {"monitoring_experiment", description_monitoring_experiment,
     additional_constraints_monitoring_experiment, nullptr, 0, true, true},
    {"multiping", description_multiping, additional_constraints_multiping,
     nullptr, 0, false, true},
    {"pick_first_new", description_pick_first_new,
     additional_constraints_pick_first_new, nullptr, 0, true, true},
    {"promise_based_inproc_transport",
     description_promise_based_inproc_transport,
     additional_constraints_promise_based_inproc_transport, nullptr, 0, false,
     false},
    {"schedule_cancellation_over_write",
     description_schedule_cancellation_over_write,
     additional_constraints_schedule_cancellation_over_write, nullptr, 0, false,
     true},
    {"server_privacy", description_server_privacy,
     additional_constraints_server_privacy, nullptr, 0, false, false},
    {"tcp_frame_size_tuning", description_tcp_frame_size_tuning,
     additional_constraints_tcp_frame_size_tuning, nullptr, 0, false, true},
    {"tcp_rcv_lowat", description_tcp_rcv_lowat,
     additional_constraints_tcp_rcv_lowat, nullptr, 0, false, true},
    {"time_caching_in_party", description_time_caching_in_party,
     additional_constraints_time_caching_in_party, nullptr, 0, true, true},
    {"trace_record_callops", description_trace_record_callops,
     additional_constraints_trace_record_callops, nullptr, 0, true, true},
    {"unconstrained_max_quota_buffer_size",
     description_unconstrained_max_quota_buffer_size,
     additional_constraints_unconstrained_max_quota_buffer_size, nullptr, 0,
     false, true},
    {"work_serializer_clears_time_cache",
     description_work_serializer_clears_time_cache,
     additional_constraints_work_serializer_clears_time_cache, nullptr, 0, true,
     true},
    {"work_serializer_dispatch", description_work_serializer_dispatch,
     additional_constraints_work_serializer_dispatch, nullptr, 0, false, true},
};

}  // namespace grpc_core

#elif defined(GPR_WINDOWS)
namespace {
const char* const description_call_tracer_in_transport =
    "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const char* const description_canary_client_privacy =
    "If set, canary client privacy";
const char* const additional_constraints_canary_client_privacy = "{}";
const char* const description_client_privacy = "If set, client privacy";
const char* const additional_constraints_client_privacy = "{}";
const char* const description_event_engine_application_callbacks =
    "Run application callbacks in EventEngine threads, instead of on the "
    "thread-local ApplicationCallbackExecCtx";
const char* const additional_constraints_event_engine_application_callbacks =
    "{}";
const char* const description_event_engine_client =
    "Use EventEngine clients instead of iomgr's grpc_tcp_client";
const char* const additional_constraints_event_engine_client = "{}";
const char* const description_event_engine_dns =
    "If set, use EventEngine DNSResolver for client channel resolution";
const char* const additional_constraints_event_engine_dns = "{}";
const char* const description_event_engine_listener =
    "Use EventEngine listeners instead of iomgr's grpc_tcp_server";
const char* const additional_constraints_event_engine_listener = "{}";
const char* const description_free_large_allocator =
    "If set, return all free bytes from a \042big\042 allocator";
const char* const additional_constraints_free_large_allocator = "{}";
const char* const description_max_pings_wo_data_throttle =
    "Experiment to throttle pings to a period of 1 min when "
    "GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA limit has reached (instead of "
    "completely blocking).";
const char* const additional_constraints_max_pings_wo_data_throttle = "{}";
const char* const description_monitoring_experiment =
    "Placeholder experiment to prove/disprove our monitoring is working";
const char* const additional_constraints_monitoring_experiment = "{}";
const char* const description_multiping =
    "Allow more than one ping to be in flight at a time by default.";
const char* const additional_constraints_multiping = "{}";
const char* const description_pick_first_new =
    "New pick_first impl with memory reduction.";
const char* const additional_constraints_pick_first_new = "{}";
const char* const description_promise_based_inproc_transport =
    "Use promises for the in-process transport.";
const char* const additional_constraints_promise_based_inproc_transport = "{}";
const char* const description_schedule_cancellation_over_write =
    "Allow cancellation op to be scheduled over a write";
const char* const additional_constraints_schedule_cancellation_over_write =
    "{}";
const char* const description_server_privacy = "If set, server privacy";
const char* const additional_constraints_server_privacy = "{}";
const char* const description_tcp_frame_size_tuning =
    "If set, enables TCP to use RPC size estimation made by higher layers. TCP "
    "would not indicate completion of a read operation until a specified "
    "number of bytes have been read over the socket. Buffers are also "
    "allocated according to estimated RPC sizes.";
const char* const additional_constraints_tcp_frame_size_tuning = "{}";
const char* const description_tcp_rcv_lowat =
    "Use SO_RCVLOWAT to avoid wakeups on the read path.";
const char* const additional_constraints_tcp_rcv_lowat = "{}";
const char* const description_time_caching_in_party =
    "Disable time caching in exec_ctx, and enable it only in a single party "
    "execution.";
const char* const additional_constraints_time_caching_in_party = "{}";
const char* const description_trace_record_callops =
    "Enables tracing of call batch initiation and completion.";
const char* const additional_constraints_trace_record_callops = "{}";
const char* const description_unconstrained_max_quota_buffer_size =
    "Discard the cap on the max free pool size for one memory allocator";
const char* const additional_constraints_unconstrained_max_quota_buffer_size =
    "{}";
const char* const description_work_serializer_clears_time_cache =
    "Have the work serializer clear the time cache when it dispatches work.";
const char* const additional_constraints_work_serializer_clears_time_cache =
    "{}";
const char* const description_work_serializer_dispatch =
    "Have the work serializer dispatch work to event engine for every "
    "callback, instead of running things inline in the first thread that "
    "successfully enqueues work.";
const char* const additional_constraints_work_serializer_dispatch = "{}";
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
    {"call_tracer_in_transport", description_call_tracer_in_transport,
     additional_constraints_call_tracer_in_transport, nullptr, 0, true, true},
    {"canary_client_privacy", description_canary_client_privacy,
     additional_constraints_canary_client_privacy, nullptr, 0, false, false},
    {"client_privacy", description_client_privacy,
     additional_constraints_client_privacy, nullptr, 0, false, false},
    {"event_engine_application_callbacks",
     description_event_engine_application_callbacks,
     additional_constraints_event_engine_application_callbacks, nullptr, 0,
     true, true},
    {"event_engine_client", description_event_engine_client,
     additional_constraints_event_engine_client, nullptr, 0, true, true},
    {"event_engine_dns", description_event_engine_dns,
     additional_constraints_event_engine_dns, nullptr, 0, true, false},
    {"event_engine_listener", description_event_engine_listener,
     additional_constraints_event_engine_listener, nullptr, 0, true, true},
    {"free_large_allocator", description_free_large_allocator,
     additional_constraints_free_large_allocator, nullptr, 0, false, true},
    {"max_pings_wo_data_throttle", description_max_pings_wo_data_throttle,
     additional_constraints_max_pings_wo_data_throttle, nullptr, 0, false,
     true},
    {"monitoring_experiment", description_monitoring_experiment,
     additional_constraints_monitoring_experiment, nullptr, 0, true, true},
    {"multiping", description_multiping, additional_constraints_multiping,
     nullptr, 0, false, true},
    {"pick_first_new", description_pick_first_new,
     additional_constraints_pick_first_new, nullptr, 0, true, true},
    {"promise_based_inproc_transport",
     description_promise_based_inproc_transport,
     additional_constraints_promise_based_inproc_transport, nullptr, 0, false,
     false},
    {"schedule_cancellation_over_write",
     description_schedule_cancellation_over_write,
     additional_constraints_schedule_cancellation_over_write, nullptr, 0, false,
     true},
    {"server_privacy", description_server_privacy,
     additional_constraints_server_privacy, nullptr, 0, false, false},
    {"tcp_frame_size_tuning", description_tcp_frame_size_tuning,
     additional_constraints_tcp_frame_size_tuning, nullptr, 0, false, true},
    {"tcp_rcv_lowat", description_tcp_rcv_lowat,
     additional_constraints_tcp_rcv_lowat, nullptr, 0, false, true},
    {"time_caching_in_party", description_time_caching_in_party,
     additional_constraints_time_caching_in_party, nullptr, 0, true, true},
    {"trace_record_callops", description_trace_record_callops,
     additional_constraints_trace_record_callops, nullptr, 0, true, true},
    {"unconstrained_max_quota_buffer_size",
     description_unconstrained_max_quota_buffer_size,
     additional_constraints_unconstrained_max_quota_buffer_size, nullptr, 0,
     false, true},
    {"work_serializer_clears_time_cache",
     description_work_serializer_clears_time_cache,
     additional_constraints_work_serializer_clears_time_cache, nullptr, 0, true,
     true},
    {"work_serializer_dispatch", description_work_serializer_dispatch,
     additional_constraints_work_serializer_dispatch, nullptr, 0, false, true},
};

}  // namespace grpc_core

#else
namespace {
const char* const description_call_tracer_in_transport =
    "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const char* const description_canary_client_privacy =
    "If set, canary client privacy";
const char* const additional_constraints_canary_client_privacy = "{}";
const char* const description_client_privacy = "If set, client privacy";
const char* const additional_constraints_client_privacy = "{}";
const char* const description_event_engine_application_callbacks =
    "Run application callbacks in EventEngine threads, instead of on the "
    "thread-local ApplicationCallbackExecCtx";
const char* const additional_constraints_event_engine_application_callbacks =
    "{}";
const char* const description_event_engine_client =
    "Use EventEngine clients instead of iomgr's grpc_tcp_client";
const char* const additional_constraints_event_engine_client = "{}";
const char* const description_event_engine_dns =
    "If set, use EventEngine DNSResolver for client channel resolution";
const char* const additional_constraints_event_engine_dns = "{}";
const char* const description_event_engine_listener =
    "Use EventEngine listeners instead of iomgr's grpc_tcp_server";
const char* const additional_constraints_event_engine_listener = "{}";
const char* const description_free_large_allocator =
    "If set, return all free bytes from a \042big\042 allocator";
const char* const additional_constraints_free_large_allocator = "{}";
const char* const description_max_pings_wo_data_throttle =
    "Experiment to throttle pings to a period of 1 min when "
    "GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA limit has reached (instead of "
    "completely blocking).";
const char* const additional_constraints_max_pings_wo_data_throttle = "{}";
const char* const description_monitoring_experiment =
    "Placeholder experiment to prove/disprove our monitoring is working";
const char* const additional_constraints_monitoring_experiment = "{}";
const char* const description_multiping =
    "Allow more than one ping to be in flight at a time by default.";
const char* const additional_constraints_multiping = "{}";
const char* const description_pick_first_new =
    "New pick_first impl with memory reduction.";
const char* const additional_constraints_pick_first_new = "{}";
const char* const description_promise_based_inproc_transport =
    "Use promises for the in-process transport.";
const char* const additional_constraints_promise_based_inproc_transport = "{}";
const char* const description_schedule_cancellation_over_write =
    "Allow cancellation op to be scheduled over a write";
const char* const additional_constraints_schedule_cancellation_over_write =
    "{}";
const char* const description_server_privacy = "If set, server privacy";
const char* const additional_constraints_server_privacy = "{}";
const char* const description_tcp_frame_size_tuning =
    "If set, enables TCP to use RPC size estimation made by higher layers. TCP "
    "would not indicate completion of a read operation until a specified "
    "number of bytes have been read over the socket. Buffers are also "
    "allocated according to estimated RPC sizes.";
const char* const additional_constraints_tcp_frame_size_tuning = "{}";
const char* const description_tcp_rcv_lowat =
    "Use SO_RCVLOWAT to avoid wakeups on the read path.";
const char* const additional_constraints_tcp_rcv_lowat = "{}";
const char* const description_time_caching_in_party =
    "Disable time caching in exec_ctx, and enable it only in a single party "
    "execution.";
const char* const additional_constraints_time_caching_in_party = "{}";
const char* const description_trace_record_callops =
    "Enables tracing of call batch initiation and completion.";
const char* const additional_constraints_trace_record_callops = "{}";
const char* const description_unconstrained_max_quota_buffer_size =
    "Discard the cap on the max free pool size for one memory allocator";
const char* const additional_constraints_unconstrained_max_quota_buffer_size =
    "{}";
const char* const description_work_serializer_clears_time_cache =
    "Have the work serializer clear the time cache when it dispatches work.";
const char* const additional_constraints_work_serializer_clears_time_cache =
    "{}";
const char* const description_work_serializer_dispatch =
    "Have the work serializer dispatch work to event engine for every "
    "callback, instead of running things inline in the first thread that "
    "successfully enqueues work.";
const char* const additional_constraints_work_serializer_dispatch = "{}";
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
    {"call_tracer_in_transport", description_call_tracer_in_transport,
     additional_constraints_call_tracer_in_transport, nullptr, 0, true, true},
    {"canary_client_privacy", description_canary_client_privacy,
     additional_constraints_canary_client_privacy, nullptr, 0, false, false},
    {"client_privacy", description_client_privacy,
     additional_constraints_client_privacy, nullptr, 0, false, false},
    {"event_engine_application_callbacks",
     description_event_engine_application_callbacks,
     additional_constraints_event_engine_application_callbacks, nullptr, 0,
     true, true},
    {"event_engine_client", description_event_engine_client,
     additional_constraints_event_engine_client, nullptr, 0, false, true},
    {"event_engine_dns", description_event_engine_dns,
     additional_constraints_event_engine_dns, nullptr, 0, true, false},
    {"event_engine_listener", description_event_engine_listener,
     additional_constraints_event_engine_listener, nullptr, 0, true, true},
    {"free_large_allocator", description_free_large_allocator,
     additional_constraints_free_large_allocator, nullptr, 0, false, true},
    {"max_pings_wo_data_throttle", description_max_pings_wo_data_throttle,
     additional_constraints_max_pings_wo_data_throttle, nullptr, 0, false,
     true},
    {"monitoring_experiment", description_monitoring_experiment,
     additional_constraints_monitoring_experiment, nullptr, 0, true, true},
    {"multiping", description_multiping, additional_constraints_multiping,
     nullptr, 0, false, true},
    {"pick_first_new", description_pick_first_new,
     additional_constraints_pick_first_new, nullptr, 0, true, true},
    {"promise_based_inproc_transport",
     description_promise_based_inproc_transport,
     additional_constraints_promise_based_inproc_transport, nullptr, 0, false,
     false},
    {"schedule_cancellation_over_write",
     description_schedule_cancellation_over_write,
     additional_constraints_schedule_cancellation_over_write, nullptr, 0, false,
     true},
    {"server_privacy", description_server_privacy,
     additional_constraints_server_privacy, nullptr, 0, false, false},
    {"tcp_frame_size_tuning", description_tcp_frame_size_tuning,
     additional_constraints_tcp_frame_size_tuning, nullptr, 0, false, true},
    {"tcp_rcv_lowat", description_tcp_rcv_lowat,
     additional_constraints_tcp_rcv_lowat, nullptr, 0, false, true},
    {"time_caching_in_party", description_time_caching_in_party,
     additional_constraints_time_caching_in_party, nullptr, 0, true, true},
    {"trace_record_callops", description_trace_record_callops,
     additional_constraints_trace_record_callops, nullptr, 0, true, true},
    {"unconstrained_max_quota_buffer_size",
     description_unconstrained_max_quota_buffer_size,
     additional_constraints_unconstrained_max_quota_buffer_size, nullptr, 0,
     false, true},
    {"work_serializer_clears_time_cache",
     description_work_serializer_clears_time_cache,
     additional_constraints_work_serializer_clears_time_cache, nullptr, 0, true,
     true},
    {"work_serializer_dispatch", description_work_serializer_dispatch,
     additional_constraints_work_serializer_dispatch, nullptr, 0, false, true},
};

}  // namespace grpc_core
#endif
#endif
