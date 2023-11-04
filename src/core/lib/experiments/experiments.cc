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

#include <grpc/support/port_platform.h>
#include "src/core/lib/experiments/experiments.h"

#ifndef GRPC_EXPERIMENTS_ARE_FINAL

#if defined(GRPC_CFSTREAM)
namespace {
const char* const description_block_excessive_requests_before_settings_ack = "If set, block excessive requests before receiving SETTINGS ACK.";
const char* const additional_constraints_block_excessive_requests_before_settings_ack = "{}";
const char* const description_call_status_override_on_cancellation = "Avoid overriding call status of successfully finished calls if it races with cancellation.";
const char* const additional_constraints_call_status_override_on_cancellation = "{}";
const char* const description_canary_client_privacy = "If set, canary client privacy";
const char* const additional_constraints_canary_client_privacy = "{}";
const char* const description_chttp2_batch_requests = "Cap the number of requests received by one transport read prior to offload.";
const char* const additional_constraints_chttp2_batch_requests = "{}";
const char* const description_chttp2_offload_on_rst_stream = "Offload work on RST_STREAM.";
const char* const additional_constraints_chttp2_offload_on_rst_stream = "{}";
const char* const description_client_idleness = "If enabled, client channel idleness is enabled by default.";
const char* const additional_constraints_client_idleness = "{}";
const char* const description_client_privacy = "If set, client privacy";
const char* const additional_constraints_client_privacy = "{}";
const char* const description_combiner_offload_to_event_engine = "Offload Combiner work onto the EventEngine instead of the Executor.";
const char* const additional_constraints_combiner_offload_to_event_engine = "{}";
const char* const description_event_engine_client = "Use EventEngine clients instead of iomgr's grpc_tcp_client";
const char* const additional_constraints_event_engine_client = "{}";
const char* const description_event_engine_dns = "If set, use EventEngine DNSResolver for client channel resolution";
const char* const additional_constraints_event_engine_dns = "{}";
const char* const description_event_engine_listener = "Use EventEngine listeners instead of iomgr's grpc_tcp_server";
const char* const additional_constraints_event_engine_listener = "{}";
const char* const description_free_large_allocator = "If set, return all free bytes from a \042big\042 allocator";
const char* const additional_constraints_free_large_allocator = "{}";
const char* const description_http2_stats_fix = "Fix on HTTP2 outgoing data stats reporting";
const char* const additional_constraints_http2_stats_fix = "{}";
const char* const description_keepalive_fix = "Allows overriding keepalive_permit_without_calls. Refer https://github.com/grpc/grpc/pull/33428 for more information.";
const char* const additional_constraints_keepalive_fix = "{}";
const char* const description_keepalive_server_fix = "Allows overriding keepalive_permit_without_calls for servers. Refer https://github.com/grpc/grpc/pull/33917 for more information.";
const char* const additional_constraints_keepalive_server_fix = "{}";
const char* const description_lazier_stream_updates = "Allow streams to consume up to 50% of the incoming window before we force send a flow control update.";
const char* const additional_constraints_lazier_stream_updates = "{}";
const char* const description_memory_pressure_controller = "New memory pressure controller";
const char* const additional_constraints_memory_pressure_controller = "{}";
const char* const description_monitoring_experiment = "Placeholder experiment to prove/disprove our monitoring is working";
const char* const additional_constraints_monitoring_experiment = "{}";
const char* const description_multiping = "Allow more than one ping to be in flight at a time by default.";
const char* const additional_constraints_multiping = "{}";
const char* const description_overload_protection = "If chttp2 has more streams than it can handle open, send RST_STREAM immediately on new streams appearing.";
const char* const additional_constraints_overload_protection = "{}";
const char* const description_peer_state_based_framing = "If set, the max sizes of frames sent to lower layers is controlled based on the peer's memory pressure which is reflected in its max http2 frame size.";
const char* const additional_constraints_peer_state_based_framing = "{}";
const char* const description_pending_queue_cap = "In the sync & async apis (but not the callback api), cap the number of received but unrequested requests in the server for each call type. A received message is one that was read from the wire on the server. A requested message is one explicitly requested by the application using grpc_server_request_call or grpc_server_request_registered_call (or their wrappers in the C++ API).";
const char* const additional_constraints_pending_queue_cap = "{}";
const char* const description_pick_first_happy_eyeballs = "Use Happy Eyeballs in pick_first.";
const char* const additional_constraints_pick_first_happy_eyeballs = "{}";
const char* const description_ping_on_rst_stream = "Send a ping on receiving some RST_STREAM frames on the server (proportion configurable via grpc.http2.ping_on_rst_stream_percent channel arg).";
const char* const additional_constraints_ping_on_rst_stream = "{}";
const char* const description_promise_based_client_call = "If set, use the new gRPC promise based call code when it's appropriate (ie when all filters in a stack are promise based)";
const char* const additional_constraints_promise_based_client_call = "{}";
const char* const description_promise_based_inproc_transport = "Use promises for the in-process transport.";
const char* const additional_constraints_promise_based_inproc_transport = "{}";
const char* const description_promise_based_server_call = "If set, use the new gRPC promise based call code when it's appropriate (ie when all filters in a stack are promise based)";
const char* const additional_constraints_promise_based_server_call = "{}";
