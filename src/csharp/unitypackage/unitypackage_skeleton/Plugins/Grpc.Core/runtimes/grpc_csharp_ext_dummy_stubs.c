
// Copyright 2019 The gRPC Authors
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

// When building for Unity Android with il2cpp backend, Unity tries to link
// the __Internal PInvoke definitions (which are required by iOS) even though
// the .so/.dll will be actually used. This file provides dummy stubs to
// make il2cpp happy.
// See https://github.com/grpc/grpc/issues/16012

#include <stdio.h>
#include <stdlib.h>

void grpcsharp_init() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_shutdown() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_version_string() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_batch_context_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_batch_context_recv_initial_metadata() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_batch_context_recv_message_length() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_batch_context_recv_message_next_slice_peek() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_batch_context_recv_status_on_client_status() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_batch_context_recv_status_on_client_details() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_batch_context_recv_status_on_client_trailing_metadata() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_batch_context_recv_close_on_server_cancelled() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_batch_context_reset() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_batch_context_destroy() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_request_call_context_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_request_call_context_call() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_request_call_context_method() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_request_call_context_host() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_request_call_context_deadline() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_request_call_context_request_metadata() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_request_call_context_reset() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_request_call_context_destroy() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_composite_call_credentials_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_credentials_release() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_cancel() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_cancel_with_status() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_start_unary() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_start_client_streaming() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_start_server_streaming() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_start_duplex_streaming() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_send_message() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_send_close_from_client() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_send_status_from_server() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_recv_message() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_recv_initial_metadata() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_start_serverside() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_send_initial_metadata() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_set_credentials() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_get_peer() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_destroy() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_channel_args_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_channel_args_set_string() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_channel_args_set_integer() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_channel_args_destroy() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_override_default_ssl_roots() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_ssl_credentials_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_composite_channel_credentials_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_channel_credentials_release() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_insecure_channel_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_secure_channel_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_channel_create_call() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_channel_check_connectivity_state() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_channel_watch_connectivity_state() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_channel_get_target() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_channel_destroy() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_sizeof_grpc_event() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_completion_queue_create_async() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_completion_queue_create_sync() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_completion_queue_shutdown() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_completion_queue_next() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_completion_queue_pluck() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_completion_queue_destroy() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void gprsharp_free() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_metadata_array_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_metadata_array_add() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_metadata_array_count() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_metadata_array_get_key() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_metadata_array_get_value() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_metadata_array_destroy_full() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_redirect_log() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_native_callback_dispatcher_init() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_metadata_credentials_create_from_plugin() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_metadata_credentials_notify_from_plugin() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_ssl_server_credentials_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_server_credentials_release() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_server_create() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_server_register_completion_queue() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_server_add_insecure_http2_port() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_server_add_secure_http2_port() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_server_start() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_server_request_call() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_server_cancel_all_calls() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_server_shutdown_and_notify_callback() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_server_destroy() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_call_auth_context() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_auth_context_peer_identity_property_name() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_auth_context_property_iterator() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_auth_property_iterator_next() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_auth_context_release() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void gprsharp_now() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void gprsharp_inf_future() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void gprsharp_inf_past() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void gprsharp_convert_clock_type() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void gprsharp_sizeof_timespec() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_test_callback() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_test_nop() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_test_override_method() {
  fprintf(stderr, "Should never reach here");
  abort();
}
void grpcsharp_test_call_start_unary_echo() {
  fprintf(stderr, "Should never reach here");
  abort();
}
