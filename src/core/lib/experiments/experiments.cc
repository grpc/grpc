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
#include <stdint.h>

#ifndef GRPC_EXPERIMENTS_ARE_FINAL

#if defined(GRPC_CFSTREAM)
namespace {
const char* const description_backoff_cap_initial_at_max =
    "Backoff library applies max_backoff even on initial_backoff.";
const char* const additional_constraints_backoff_cap_initial_at_max = "{}";
const char* const description_call_tracer_in_transport =
    "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const char* const description_call_tracer_transport_fix =
    "Use the correct call tracer in transport";
const char* const additional_constraints_call_tracer_transport_fix = "{}";
const char* const description_callv3_client_auth_filter =
    "Use the CallV3 client auth filter.";
const char* const additional_constraints_callv3_client_auth_filter = "{}";
const char* const description_chaotic_good_framing_layer =
    "Enable the chaotic good framing layer.";
const char* const additional_constraints_chaotic_good_framing_layer = "{}";
const char* const description_event_engine_client =
    "Use EventEngine clients instead of iomgr's grpc_tcp_client";
const char* const additional_constraints_event_engine_client = "{}";
const char* const description_event_engine_dns =
    "If set, use EventEngine DNSResolver for client channel resolution";
const char* const additional_constraints_event_engine_dns = "{}";
const char* const description_event_engine_dns_non_client_channel =
    "If set, use EventEngine DNSResolver in other places besides client "
    "channel.";
const char* const additional_constraints_event_engine_dns_non_client_channel =
    "{}";
const char* const description_event_engine_listener =
    "Use EventEngine listeners instead of iomgr's grpc_tcp_server";
const char* const additional_constraints_event_engine_listener = "{}";
const char* const description_event_engine_callback_cq =
    "Use EventEngine instead of the CallbackAlternativeCQ.";
const char* const additional_constraints_event_engine_callback_cq = "{}";
const uint8_t required_experiments_event_engine_callback_cq[] = {
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineClient),
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineListener)};
const char* const description_event_engine_for_all_other_endpoints =
    "Use EventEngine endpoints for all call sites, including direct uses of "
    "grpc_tcp_create.";
const char* const additional_constraints_event_engine_for_all_other_endpoints =
    "{}";
const uint8_t required_experiments_event_engine_for_all_other_endpoints[] = {
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineClient),
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineDns),
    static_cast<uint8_t>(
        grpc_core::kExperimentIdEventEngineDnsNonClientChannel),
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineListener)};
const char* const description_free_large_allocator =
    "If set, return all free bytes from a \042big\042 allocator";
const char* const additional_constraints_free_large_allocator = "{}";
const char* const description_keep_alive_ping_timer_batch =
    "Avoid explicitly cancelling the keepalive timer. Instead adjust the "
    "callback to re-schedule itself to the next ping interval.";
const char* const additional_constraints_keep_alive_ping_timer_batch = "{}";
const char* const description_local_connector_secure =
    "Local security connector uses TSI_SECURITY_NONE for LOCAL_TCP "
    "connections.";
const char* const additional_constraints_local_connector_secure = "{}";
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
const char* const description_posix_ee_skip_grpc_init =
    "Prevent the PosixEventEngine from calling grpc_init & grpc_shutdown on "
    "creation and destruction.";
const char* const additional_constraints_posix_ee_skip_grpc_init = "{}";
const char* const description_promise_based_http2_client_transport =
    "Use promises for the http2 client transport. We have kept client and "
    "server transport experiments separate to help with smoother roll outs and "
    "also help with interop testing.";
const char* const additional_constraints_promise_based_http2_client_transport =
    "{}";
const char* const description_promise_based_http2_server_transport =
    "Use promises for the http2 server transport. We have kept client and "
    "server transport experiments separate to help with smoother roll outs and "
    "also help with interop testing.";
const char* const additional_constraints_promise_based_http2_server_transport =
    "{}";
const char* const description_promise_based_inproc_transport =
    "Use promises for the in-process transport.";
const char* const additional_constraints_promise_based_inproc_transport = "{}";
const char* const description_retry_in_callv3 = "Support retries with call-v3";
const char* const additional_constraints_retry_in_callv3 = "{}";
const char* const description_rq_fast_reject =
    "Resource quota rejects requests immediately (before allocating the "
    "request structure) under very high memory pressure.";
const char* const additional_constraints_rq_fast_reject = "{}";
const char* const description_rst_stream_fix =
    "Fix for RST_STREAM - do not send for idle streams "
    "(https://github.com/grpc/grpc/issues/38758)";
const char* const additional_constraints_rst_stream_fix = "{}";
const char* const description_schedule_cancellation_over_write =
    "Allow cancellation op to be scheduled over a write";
const char* const additional_constraints_schedule_cancellation_over_write =
    "{}";
const char* const description_server_listener =
    "If set, the new server listener classes are used.";
const char* const additional_constraints_server_listener = "{}";
const char* const description_tcp_frame_size_tuning =
    "If set, enables TCP to use RPC size estimation made by higher layers. TCP "
    "would not indicate completion of a read operation until a specified "
    "number of bytes have been read over the socket. Buffers are also "
    "allocated according to estimated RPC sizes.";
const char* const additional_constraints_tcp_frame_size_tuning = "{}";
const char* const description_tcp_rcv_lowat =
    "Use SO_RCVLOWAT to avoid wakeups on the read path.";
const char* const additional_constraints_tcp_rcv_lowat = "{}";
const char* const description_unconstrained_max_quota_buffer_size =
    "Discard the cap on the max free pool size for one memory allocator";
const char* const additional_constraints_unconstrained_max_quota_buffer_size =
    "{}";
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
    {"backoff_cap_initial_at_max", description_backoff_cap_initial_at_max,
     additional_constraints_backoff_cap_initial_at_max, nullptr, 0, true, true},
    {"call_tracer_in_transport", description_call_tracer_in_transport,
     additional_constraints_call_tracer_in_transport, nullptr, 0, true, false},
    {"call_tracer_transport_fix", description_call_tracer_transport_fix,
     additional_constraints_call_tracer_transport_fix, nullptr, 0, true, true},
    {"callv3_client_auth_filter", description_callv3_client_auth_filter,
     additional_constraints_callv3_client_auth_filter, nullptr, 0, false, true},
    {"chaotic_good_framing_layer", description_chaotic_good_framing_layer,
     additional_constraints_chaotic_good_framing_layer, nullptr, 0, false,
     true},
    {"event_engine_client", description_event_engine_client,
     additional_constraints_event_engine_client, nullptr, 0, false, false},
    {"event_engine_dns", description_event_engine_dns,
     additional_constraints_event_engine_dns, nullptr, 0, false, false},
    {"event_engine_dns_non_client_channel",
     description_event_engine_dns_non_client_channel,
     additional_constraints_event_engine_dns_non_client_channel, nullptr, 0,
     false, false},
    {"event_engine_listener", description_event_engine_listener,
     additional_constraints_event_engine_listener, nullptr, 0, false, false},
    {"event_engine_callback_cq", description_event_engine_callback_cq,
     additional_constraints_event_engine_callback_cq,
     required_experiments_event_engine_callback_cq, 2, true, true},
    {"event_engine_for_all_other_endpoints",
     description_event_engine_for_all_other_endpoints,
     additional_constraints_event_engine_for_all_other_endpoints,
     required_experiments_event_engine_for_all_other_endpoints, 4, true, false},
    {"free_large_allocator", description_free_large_allocator,
     additional_constraints_free_large_allocator, nullptr, 0, false, true},
    {"keep_alive_ping_timer_batch", description_keep_alive_ping_timer_batch,
     additional_constraints_keep_alive_ping_timer_batch, nullptr, 0, false,
     true},
    {"local_connector_secure", description_local_connector_secure,
     additional_constraints_local_connector_secure, nullptr, 0, false, true},
    {"max_pings_wo_data_throttle", description_max_pings_wo_data_throttle,
     additional_constraints_max_pings_wo_data_throttle, nullptr, 0, true, true},
    {"monitoring_experiment", description_monitoring_experiment,
     additional_constraints_monitoring_experiment, nullptr, 0, true, true},
    {"multiping", description_multiping, additional_constraints_multiping,
     nullptr, 0, false, true},
    {"posix_ee_skip_grpc_init", description_posix_ee_skip_grpc_init,
     additional_constraints_posix_ee_skip_grpc_init, nullptr, 0, true, true},
    {"promise_based_http2_client_transport",
     description_promise_based_http2_client_transport,
     additional_constraints_promise_based_http2_client_transport, nullptr, 0,
     false, true},
    {"promise_based_http2_server_transport",
     description_promise_based_http2_server_transport,
     additional_constraints_promise_based_http2_server_transport, nullptr, 0,
     false, true},
    {"promise_based_inproc_transport",
     description_promise_based_inproc_transport,
     additional_constraints_promise_based_inproc_transport, nullptr, 0, false,
     false},
    {"retry_in_callv3", description_retry_in_callv3,
     additional_constraints_retry_in_callv3, nullptr, 0, false, true},
    {"rq_fast_reject", description_rq_fast_reject,
     additional_constraints_rq_fast_reject, nullptr, 0, false, true},
    {"rst_stream_fix", description_rst_stream_fix,
     additional_constraints_rst_stream_fix, nullptr, 0, true, true},
    {"schedule_cancellation_over_write",
     description_schedule_cancellation_over_write,
     additional_constraints_schedule_cancellation_over_write, nullptr, 0, false,
     true},
    {"server_listener", description_server_listener,
     additional_constraints_server_listener, nullptr, 0, true, true},
    {"tcp_frame_size_tuning", description_tcp_frame_size_tuning,
     additional_constraints_tcp_frame_size_tuning, nullptr, 0, false, true},
    {"tcp_rcv_lowat", description_tcp_rcv_lowat,
     additional_constraints_tcp_rcv_lowat, nullptr, 0, false, true},
    {"unconstrained_max_quota_buffer_size",
     description_unconstrained_max_quota_buffer_size,
     additional_constraints_unconstrained_max_quota_buffer_size, nullptr, 0,
     false, true},
};

}  // namespace grpc_core

#elif defined(GPR_WINDOWS)
namespace {
const char* const description_backoff_cap_initial_at_max =
    "Backoff library applies max_backoff even on initial_backoff.";
const char* const additional_constraints_backoff_cap_initial_at_max = "{}";
const char* const description_call_tracer_in_transport =
    "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const char* const description_call_tracer_transport_fix =
    "Use the correct call tracer in transport";
const char* const additional_constraints_call_tracer_transport_fix = "{}";
const char* const description_callv3_client_auth_filter =
    "Use the CallV3 client auth filter.";
const char* const additional_constraints_callv3_client_auth_filter = "{}";
const char* const description_chaotic_good_framing_layer =
    "Enable the chaotic good framing layer.";
const char* const additional_constraints_chaotic_good_framing_layer = "{}";
const char* const description_event_engine_client =
    "Use EventEngine clients instead of iomgr's grpc_tcp_client";
const char* const additional_constraints_event_engine_client = "{}";
const char* const description_event_engine_dns =
    "If set, use EventEngine DNSResolver for client channel resolution";
const char* const additional_constraints_event_engine_dns = "{}";
const char* const description_event_engine_dns_non_client_channel =
    "If set, use EventEngine DNSResolver in other places besides client "
    "channel.";
const char* const additional_constraints_event_engine_dns_non_client_channel =
    "{}";
const char* const description_event_engine_listener =
    "Use EventEngine listeners instead of iomgr's grpc_tcp_server";
const char* const additional_constraints_event_engine_listener = "{}";
const char* const description_event_engine_callback_cq =
    "Use EventEngine instead of the CallbackAlternativeCQ.";
const char* const additional_constraints_event_engine_callback_cq = "{}";
const uint8_t required_experiments_event_engine_callback_cq[] = {
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineClient),
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineListener)};
const char* const description_event_engine_for_all_other_endpoints =
    "Use EventEngine endpoints for all call sites, including direct uses of "
    "grpc_tcp_create.";
const char* const additional_constraints_event_engine_for_all_other_endpoints =
    "{}";
const uint8_t required_experiments_event_engine_for_all_other_endpoints[] = {
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineClient),
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineDns),
    static_cast<uint8_t>(
        grpc_core::kExperimentIdEventEngineDnsNonClientChannel),
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineListener)};
const char* const description_free_large_allocator =
    "If set, return all free bytes from a \042big\042 allocator";
const char* const additional_constraints_free_large_allocator = "{}";
const char* const description_keep_alive_ping_timer_batch =
    "Avoid explicitly cancelling the keepalive timer. Instead adjust the "
    "callback to re-schedule itself to the next ping interval.";
const char* const additional_constraints_keep_alive_ping_timer_batch = "{}";
const char* const description_local_connector_secure =
    "Local security connector uses TSI_SECURITY_NONE for LOCAL_TCP "
    "connections.";
const char* const additional_constraints_local_connector_secure = "{}";
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
const char* const description_posix_ee_skip_grpc_init =
    "Prevent the PosixEventEngine from calling grpc_init & grpc_shutdown on "
    "creation and destruction.";
const char* const additional_constraints_posix_ee_skip_grpc_init = "{}";
const char* const description_promise_based_http2_client_transport =
    "Use promises for the http2 client transport. We have kept client and "
    "server transport experiments separate to help with smoother roll outs and "
    "also help with interop testing.";
const char* const additional_constraints_promise_based_http2_client_transport =
    "{}";
const char* const description_promise_based_http2_server_transport =
    "Use promises for the http2 server transport. We have kept client and "
    "server transport experiments separate to help with smoother roll outs and "
    "also help with interop testing.";
const char* const additional_constraints_promise_based_http2_server_transport =
    "{}";
const char* const description_promise_based_inproc_transport =
    "Use promises for the in-process transport.";
const char* const additional_constraints_promise_based_inproc_transport = "{}";
const char* const description_retry_in_callv3 = "Support retries with call-v3";
const char* const additional_constraints_retry_in_callv3 = "{}";
const char* const description_rq_fast_reject =
    "Resource quota rejects requests immediately (before allocating the "
    "request structure) under very high memory pressure.";
const char* const additional_constraints_rq_fast_reject = "{}";
const char* const description_rst_stream_fix =
    "Fix for RST_STREAM - do not send for idle streams "
    "(https://github.com/grpc/grpc/issues/38758)";
const char* const additional_constraints_rst_stream_fix = "{}";
const char* const description_schedule_cancellation_over_write =
    "Allow cancellation op to be scheduled over a write";
const char* const additional_constraints_schedule_cancellation_over_write =
    "{}";
const char* const description_server_listener =
    "If set, the new server listener classes are used.";
const char* const additional_constraints_server_listener = "{}";
const char* const description_tcp_frame_size_tuning =
    "If set, enables TCP to use RPC size estimation made by higher layers. TCP "
    "would not indicate completion of a read operation until a specified "
    "number of bytes have been read over the socket. Buffers are also "
    "allocated according to estimated RPC sizes.";
const char* const additional_constraints_tcp_frame_size_tuning = "{}";
const char* const description_tcp_rcv_lowat =
    "Use SO_RCVLOWAT to avoid wakeups on the read path.";
const char* const additional_constraints_tcp_rcv_lowat = "{}";
const char* const description_unconstrained_max_quota_buffer_size =
    "Discard the cap on the max free pool size for one memory allocator";
const char* const additional_constraints_unconstrained_max_quota_buffer_size =
    "{}";
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
    {"backoff_cap_initial_at_max", description_backoff_cap_initial_at_max,
     additional_constraints_backoff_cap_initial_at_max, nullptr, 0, true, true},
    {"call_tracer_in_transport", description_call_tracer_in_transport,
     additional_constraints_call_tracer_in_transport, nullptr, 0, true, false},
    {"call_tracer_transport_fix", description_call_tracer_transport_fix,
     additional_constraints_call_tracer_transport_fix, nullptr, 0, true, true},
    {"callv3_client_auth_filter", description_callv3_client_auth_filter,
     additional_constraints_callv3_client_auth_filter, nullptr, 0, false, true},
    {"chaotic_good_framing_layer", description_chaotic_good_framing_layer,
     additional_constraints_chaotic_good_framing_layer, nullptr, 0, false,
     true},
    {"event_engine_client", description_event_engine_client,
     additional_constraints_event_engine_client, nullptr, 0, true, false},
    {"event_engine_dns", description_event_engine_dns,
     additional_constraints_event_engine_dns, nullptr, 0, true, false},
    {"event_engine_dns_non_client_channel",
     description_event_engine_dns_non_client_channel,
     additional_constraints_event_engine_dns_non_client_channel, nullptr, 0,
     false, false},
    {"event_engine_listener", description_event_engine_listener,
     additional_constraints_event_engine_listener, nullptr, 0, true, false},
    {"event_engine_callback_cq", description_event_engine_callback_cq,
     additional_constraints_event_engine_callback_cq,
     required_experiments_event_engine_callback_cq, 2, true, true},
    {"event_engine_for_all_other_endpoints",
     description_event_engine_for_all_other_endpoints,
     additional_constraints_event_engine_for_all_other_endpoints,
     required_experiments_event_engine_for_all_other_endpoints, 4, true, false},
    {"free_large_allocator", description_free_large_allocator,
     additional_constraints_free_large_allocator, nullptr, 0, false, true},
    {"keep_alive_ping_timer_batch", description_keep_alive_ping_timer_batch,
     additional_constraints_keep_alive_ping_timer_batch, nullptr, 0, false,
     true},
    {"local_connector_secure", description_local_connector_secure,
     additional_constraints_local_connector_secure, nullptr, 0, false, true},
    {"max_pings_wo_data_throttle", description_max_pings_wo_data_throttle,
     additional_constraints_max_pings_wo_data_throttle, nullptr, 0, true, true},
    {"monitoring_experiment", description_monitoring_experiment,
     additional_constraints_monitoring_experiment, nullptr, 0, true, true},
    {"multiping", description_multiping, additional_constraints_multiping,
     nullptr, 0, false, true},
    {"posix_ee_skip_grpc_init", description_posix_ee_skip_grpc_init,
     additional_constraints_posix_ee_skip_grpc_init, nullptr, 0, true, true},
    {"promise_based_http2_client_transport",
     description_promise_based_http2_client_transport,
     additional_constraints_promise_based_http2_client_transport, nullptr, 0,
     false, true},
    {"promise_based_http2_server_transport",
     description_promise_based_http2_server_transport,
     additional_constraints_promise_based_http2_server_transport, nullptr, 0,
     false, true},
    {"promise_based_inproc_transport",
     description_promise_based_inproc_transport,
     additional_constraints_promise_based_inproc_transport, nullptr, 0, false,
     false},
    {"retry_in_callv3", description_retry_in_callv3,
     additional_constraints_retry_in_callv3, nullptr, 0, false, true},
    {"rq_fast_reject", description_rq_fast_reject,
     additional_constraints_rq_fast_reject, nullptr, 0, false, true},
    {"rst_stream_fix", description_rst_stream_fix,
     additional_constraints_rst_stream_fix, nullptr, 0, true, true},
    {"schedule_cancellation_over_write",
     description_schedule_cancellation_over_write,
     additional_constraints_schedule_cancellation_over_write, nullptr, 0, false,
     true},
    {"server_listener", description_server_listener,
     additional_constraints_server_listener, nullptr, 0, true, true},
    {"tcp_frame_size_tuning", description_tcp_frame_size_tuning,
     additional_constraints_tcp_frame_size_tuning, nullptr, 0, false, true},
    {"tcp_rcv_lowat", description_tcp_rcv_lowat,
     additional_constraints_tcp_rcv_lowat, nullptr, 0, false, true},
    {"unconstrained_max_quota_buffer_size",
     description_unconstrained_max_quota_buffer_size,
     additional_constraints_unconstrained_max_quota_buffer_size, nullptr, 0,
     false, true},
};

}  // namespace grpc_core

#else
namespace {
const char* const description_backoff_cap_initial_at_max =
    "Backoff library applies max_backoff even on initial_backoff.";
const char* const additional_constraints_backoff_cap_initial_at_max = "{}";
const char* const description_call_tracer_in_transport =
    "Transport directly passes byte counts to CallTracer.";
const char* const additional_constraints_call_tracer_in_transport = "{}";
const char* const description_call_tracer_transport_fix =
    "Use the correct call tracer in transport";
const char* const additional_constraints_call_tracer_transport_fix = "{}";
const char* const description_callv3_client_auth_filter =
    "Use the CallV3 client auth filter.";
const char* const additional_constraints_callv3_client_auth_filter = "{}";
const char* const description_chaotic_good_framing_layer =
    "Enable the chaotic good framing layer.";
const char* const additional_constraints_chaotic_good_framing_layer = "{}";
const char* const description_event_engine_client =
    "Use EventEngine clients instead of iomgr's grpc_tcp_client";
const char* const additional_constraints_event_engine_client = "{}";
const char* const description_event_engine_dns =
    "If set, use EventEngine DNSResolver for client channel resolution";
const char* const additional_constraints_event_engine_dns = "{}";
const char* const description_event_engine_dns_non_client_channel =
    "If set, use EventEngine DNSResolver in other places besides client "
    "channel.";
const char* const additional_constraints_event_engine_dns_non_client_channel =
    "{}";
const char* const description_event_engine_listener =
    "Use EventEngine listeners instead of iomgr's grpc_tcp_server";
const char* const additional_constraints_event_engine_listener = "{}";
const char* const description_event_engine_callback_cq =
    "Use EventEngine instead of the CallbackAlternativeCQ.";
const char* const additional_constraints_event_engine_callback_cq = "{}";
const uint8_t required_experiments_event_engine_callback_cq[] = {
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineClient),
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineListener)};
const char* const description_event_engine_for_all_other_endpoints =
    "Use EventEngine endpoints for all call sites, including direct uses of "
    "grpc_tcp_create.";
const char* const additional_constraints_event_engine_for_all_other_endpoints =
    "{}";
const uint8_t required_experiments_event_engine_for_all_other_endpoints[] = {
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineClient),
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineDns),
    static_cast<uint8_t>(
        grpc_core::kExperimentIdEventEngineDnsNonClientChannel),
    static_cast<uint8_t>(grpc_core::kExperimentIdEventEngineListener)};
const char* const description_free_large_allocator =
    "If set, return all free bytes from a \042big\042 allocator";
const char* const additional_constraints_free_large_allocator = "{}";
const char* const description_keep_alive_ping_timer_batch =
    "Avoid explicitly cancelling the keepalive timer. Instead adjust the "
    "callback to re-schedule itself to the next ping interval.";
const char* const additional_constraints_keep_alive_ping_timer_batch = "{}";
const char* const description_local_connector_secure =
    "Local security connector uses TSI_SECURITY_NONE for LOCAL_TCP "
    "connections.";
const char* const additional_constraints_local_connector_secure = "{}";
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
const char* const description_posix_ee_skip_grpc_init =
    "Prevent the PosixEventEngine from calling grpc_init & grpc_shutdown on "
    "creation and destruction.";
const char* const additional_constraints_posix_ee_skip_grpc_init = "{}";
const char* const description_promise_based_http2_client_transport =
    "Use promises for the http2 client transport. We have kept client and "
    "server transport experiments separate to help with smoother roll outs and "
    "also help with interop testing.";
const char* const additional_constraints_promise_based_http2_client_transport =
    "{}";
const char* const description_promise_based_http2_server_transport =
    "Use promises for the http2 server transport. We have kept client and "
    "server transport experiments separate to help with smoother roll outs and "
    "also help with interop testing.";
const char* const additional_constraints_promise_based_http2_server_transport =
    "{}";
const char* const description_promise_based_inproc_transport =
    "Use promises for the in-process transport.";
const char* const additional_constraints_promise_based_inproc_transport = "{}";
const char* const description_retry_in_callv3 = "Support retries with call-v3";
const char* const additional_constraints_retry_in_callv3 = "{}";
const char* const description_rq_fast_reject =
    "Resource quota rejects requests immediately (before allocating the "
    "request structure) under very high memory pressure.";
const char* const additional_constraints_rq_fast_reject = "{}";
const char* const description_rst_stream_fix =
    "Fix for RST_STREAM - do not send for idle streams "
    "(https://github.com/grpc/grpc/issues/38758)";
const char* const additional_constraints_rst_stream_fix = "{}";
const char* const description_schedule_cancellation_over_write =
    "Allow cancellation op to be scheduled over a write";
const char* const additional_constraints_schedule_cancellation_over_write =
    "{}";
const char* const description_server_listener =
    "If set, the new server listener classes are used.";
const char* const additional_constraints_server_listener = "{}";
const char* const description_tcp_frame_size_tuning =
    "If set, enables TCP to use RPC size estimation made by higher layers. TCP "
    "would not indicate completion of a read operation until a specified "
    "number of bytes have been read over the socket. Buffers are also "
    "allocated according to estimated RPC sizes.";
const char* const additional_constraints_tcp_frame_size_tuning = "{}";
const char* const description_tcp_rcv_lowat =
    "Use SO_RCVLOWAT to avoid wakeups on the read path.";
const char* const additional_constraints_tcp_rcv_lowat = "{}";
const char* const description_unconstrained_max_quota_buffer_size =
    "Discard the cap on the max free pool size for one memory allocator";
const char* const additional_constraints_unconstrained_max_quota_buffer_size =
    "{}";
}  // namespace

namespace grpc_core {

const ExperimentMetadata g_experiment_metadata[] = {
    {"backoff_cap_initial_at_max", description_backoff_cap_initial_at_max,
     additional_constraints_backoff_cap_initial_at_max, nullptr, 0, true, true},
    {"call_tracer_in_transport", description_call_tracer_in_transport,
     additional_constraints_call_tracer_in_transport, nullptr, 0, true, false},
    {"call_tracer_transport_fix", description_call_tracer_transport_fix,
     additional_constraints_call_tracer_transport_fix, nullptr, 0, true, true},
    {"callv3_client_auth_filter", description_callv3_client_auth_filter,
     additional_constraints_callv3_client_auth_filter, nullptr, 0, false, true},
    {"chaotic_good_framing_layer", description_chaotic_good_framing_layer,
     additional_constraints_chaotic_good_framing_layer, nullptr, 0, false,
     true},
    {"event_engine_client", description_event_engine_client,
     additional_constraints_event_engine_client, nullptr, 0, true, false},
    {"event_engine_dns", description_event_engine_dns,
     additional_constraints_event_engine_dns, nullptr, 0, true, false},
    {"event_engine_dns_non_client_channel",
     description_event_engine_dns_non_client_channel,
     additional_constraints_event_engine_dns_non_client_channel, nullptr, 0,
     true, false},
    {"event_engine_listener", description_event_engine_listener,
     additional_constraints_event_engine_listener, nullptr, 0, true, false},
    {"event_engine_callback_cq", description_event_engine_callback_cq,
     additional_constraints_event_engine_callback_cq,
     required_experiments_event_engine_callback_cq, 2, true, true},
    {"event_engine_for_all_other_endpoints",
     description_event_engine_for_all_other_endpoints,
     additional_constraints_event_engine_for_all_other_endpoints,
     required_experiments_event_engine_for_all_other_endpoints, 4, true, false},
    {"free_large_allocator", description_free_large_allocator,
     additional_constraints_free_large_allocator, nullptr, 0, false, true},
    {"keep_alive_ping_timer_batch", description_keep_alive_ping_timer_batch,
     additional_constraints_keep_alive_ping_timer_batch, nullptr, 0, false,
     true},
    {"local_connector_secure", description_local_connector_secure,
     additional_constraints_local_connector_secure, nullptr, 0, false, true},
    {"max_pings_wo_data_throttle", description_max_pings_wo_data_throttle,
     additional_constraints_max_pings_wo_data_throttle, nullptr, 0, true, true},
    {"monitoring_experiment", description_monitoring_experiment,
     additional_constraints_monitoring_experiment, nullptr, 0, true, true},
    {"multiping", description_multiping, additional_constraints_multiping,
     nullptr, 0, false, true},
    {"posix_ee_skip_grpc_init", description_posix_ee_skip_grpc_init,
     additional_constraints_posix_ee_skip_grpc_init, nullptr, 0, true, true},
    {"promise_based_http2_client_transport",
     description_promise_based_http2_client_transport,
     additional_constraints_promise_based_http2_client_transport, nullptr, 0,
     false, true},
    {"promise_based_http2_server_transport",
     description_promise_based_http2_server_transport,
     additional_constraints_promise_based_http2_server_transport, nullptr, 0,
     false, true},
    {"promise_based_inproc_transport",
     description_promise_based_inproc_transport,
     additional_constraints_promise_based_inproc_transport, nullptr, 0, false,
     false},
    {"retry_in_callv3", description_retry_in_callv3,
     additional_constraints_retry_in_callv3, nullptr, 0, false, true},
    {"rq_fast_reject", description_rq_fast_reject,
     additional_constraints_rq_fast_reject, nullptr, 0, false, true},
    {"rst_stream_fix", description_rst_stream_fix,
     additional_constraints_rst_stream_fix, nullptr, 0, true, true},
    {"schedule_cancellation_over_write",
     description_schedule_cancellation_over_write,
     additional_constraints_schedule_cancellation_over_write, nullptr, 0, false,
     true},
    {"server_listener", description_server_listener,
     additional_constraints_server_listener, nullptr, 0, true, true},
    {"tcp_frame_size_tuning", description_tcp_frame_size_tuning,
     additional_constraints_tcp_frame_size_tuning, nullptr, 0, false, true},
    {"tcp_rcv_lowat", description_tcp_rcv_lowat,
     additional_constraints_tcp_rcv_lowat, nullptr, 0, false, true},
    {"unconstrained_max_quota_buffer_size",
     description_unconstrained_max_quota_buffer_size,
     additional_constraints_unconstrained_max_quota_buffer_size, nullptr, 0,
     false, true},
};

}  // namespace grpc_core
#endif
#endif
