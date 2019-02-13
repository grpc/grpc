
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

void grpcsharp_init() {}
void grpcsharp_shutdown() {}
void grpcsharp_version_string() {}
void grpcsharp_batch_context_create() {}
void grpcsharp_batch_context_recv_initial_metadata() {}
void grpcsharp_batch_context_recv_message_length() {}
void grpcsharp_batch_context_recv_message_to_buffer() {}
void grpcsharp_batch_context_recv_status_on_client_status() {}
void grpcsharp_batch_context_recv_status_on_client_details() {}
void grpcsharp_batch_context_recv_status_on_client_trailing_metadata() {}
void grpcsharp_batch_context_recv_close_on_server_cancelled() {}
void grpcsharp_batch_context_reset() {}
void grpcsharp_batch_context_destroy() {}
void grpcsharp_request_call_context_create() {}
void grpcsharp_request_call_context_call() {}
void grpcsharp_request_call_context_method() {}
void grpcsharp_request_call_context_host() {}
void grpcsharp_request_call_context_deadline() {}
void grpcsharp_request_call_context_request_metadata() {}
void grpcsharp_request_call_context_reset() {}
void grpcsharp_request_call_context_destroy() {}
void grpcsharp_composite_call_credentials_create() {}
void grpcsharp_call_credentials_release() {}
void grpcsharp_call_cancel() {}
void grpcsharp_call_cancel_with_status() {}
void grpcsharp_call_start_unary() {}
void grpcsharp_call_start_client_streaming() {}
void grpcsharp_call_start_server_streaming() {}
void grpcsharp_call_start_duplex_streaming() {}
void grpcsharp_call_send_message() {}
void grpcsharp_call_send_close_from_client() {}
void grpcsharp_call_send_status_from_server() {}
void grpcsharp_call_recv_message() {}
void grpcsharp_call_recv_initial_metadata() {}
void grpcsharp_call_start_serverside() {}
void grpcsharp_call_send_initial_metadata() {}
void grpcsharp_call_set_credentials() {}
void grpcsharp_call_get_peer() {}
void grpcsharp_call_destroy() {}
void grpcsharp_channel_args_create() {}
void grpcsharp_channel_args_set_string() {}
void grpcsharp_channel_args_set_integer() {}
void grpcsharp_channel_args_destroy() {}
void grpcsharp_override_default_ssl_roots() {}
void grpcsharp_ssl_credentials_create() {}
void grpcsharp_composite_channel_credentials_create() {}
void grpcsharp_channel_credentials_release() {}
void grpcsharp_insecure_channel_create() {}
void grpcsharp_secure_channel_create() {}
void grpcsharp_channel_create_call() {}
void grpcsharp_channel_check_connectivity_state() {}
void grpcsharp_channel_watch_connectivity_state() {}
void grpcsharp_channel_get_target() {}
void grpcsharp_channel_destroy() {}
void grpcsharp_sizeof_grpc_event() {}
void grpcsharp_completion_queue_create_async() {}
void grpcsharp_completion_queue_create_sync() {}
void grpcsharp_completion_queue_shutdown() {}
void grpcsharp_completion_queue_next() {}
void grpcsharp_completion_queue_pluck() {}
void grpcsharp_completion_queue_destroy() {}
void gprsharp_free() {}
void grpcsharp_metadata_array_create() {}
void grpcsharp_metadata_array_add() {}
void grpcsharp_metadata_array_count() {}
void grpcsharp_metadata_array_get_key() {}
void grpcsharp_metadata_array_get_value() {}
void grpcsharp_metadata_array_destroy_full() {}
void grpcsharp_redirect_log() {}
void grpcsharp_metadata_credentials_create_from_plugin() {}
void grpcsharp_metadata_credentials_notify_from_plugin() {}
void grpcsharp_ssl_server_credentials_create() {}
void grpcsharp_server_credentials_release() {}
void grpcsharp_server_create() {}
void grpcsharp_server_register_completion_queue() {}
void grpcsharp_server_add_insecure_http2_port() {}
void grpcsharp_server_add_secure_http2_port() {}
void grpcsharp_server_start() {}
void grpcsharp_server_request_call() {}
void grpcsharp_server_cancel_all_calls() {}
void grpcsharp_server_shutdown_and_notify_callback() {}
void grpcsharp_server_destroy() {}
void grpcsharp_call_auth_context() {}
void grpcsharp_auth_context_peer_identity_property_name() {}
void grpcsharp_auth_context_property_iterator() {}
void grpcsharp_auth_property_iterator_next() {}
void grpcsharp_auth_context_release() {}
void gprsharp_now() {}
void gprsharp_inf_future() {}
void gprsharp_inf_past() {}
void gprsharp_convert_clock_type() {}
void gprsharp_sizeof_timespec() {}
void grpcsharp_test_callback() {}
void grpcsharp_test_nop() {}
void grpcsharp_test_override_method() {}
