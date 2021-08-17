
#region Copyright notice and license

// Copyright 2015 gRPC authors.
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

#endregion

using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal partial class NativeMethods
    {
        #region Native methods
        
        public readonly Delegates.grpcsharp_init_delegate grpcsharp_init;
        public readonly Delegates.grpcsharp_shutdown_delegate grpcsharp_shutdown;
        public readonly Delegates.grpcsharp_version_string_delegate grpcsharp_version_string;
        public readonly Delegates.grpcsharp_batch_context_create_delegate grpcsharp_batch_context_create;
        public readonly Delegates.grpcsharp_batch_context_recv_initial_metadata_delegate grpcsharp_batch_context_recv_initial_metadata;
        public readonly Delegates.grpcsharp_batch_context_recv_message_length_delegate grpcsharp_batch_context_recv_message_length;
        public readonly Delegates.grpcsharp_batch_context_recv_message_next_slice_peek_delegate grpcsharp_batch_context_recv_message_next_slice_peek;
        public readonly Delegates.grpcsharp_batch_context_recv_status_on_client_status_delegate grpcsharp_batch_context_recv_status_on_client_status;
        public readonly Delegates.grpcsharp_batch_context_recv_status_on_client_details_delegate grpcsharp_batch_context_recv_status_on_client_details;
        public readonly Delegates.grpcsharp_batch_context_recv_status_on_client_error_string_delegate grpcsharp_batch_context_recv_status_on_client_error_string;
        public readonly Delegates.grpcsharp_batch_context_recv_status_on_client_trailing_metadata_delegate grpcsharp_batch_context_recv_status_on_client_trailing_metadata;
        public readonly Delegates.grpcsharp_batch_context_recv_close_on_server_cancelled_delegate grpcsharp_batch_context_recv_close_on_server_cancelled;
        public readonly Delegates.grpcsharp_batch_context_reset_delegate grpcsharp_batch_context_reset;
        public readonly Delegates.grpcsharp_batch_context_destroy_delegate grpcsharp_batch_context_destroy;
        public readonly Delegates.grpcsharp_request_call_context_create_delegate grpcsharp_request_call_context_create;
        public readonly Delegates.grpcsharp_request_call_context_call_delegate grpcsharp_request_call_context_call;
        public readonly Delegates.grpcsharp_request_call_context_method_delegate grpcsharp_request_call_context_method;
        public readonly Delegates.grpcsharp_request_call_context_host_delegate grpcsharp_request_call_context_host;
        public readonly Delegates.grpcsharp_request_call_context_deadline_delegate grpcsharp_request_call_context_deadline;
        public readonly Delegates.grpcsharp_request_call_context_request_metadata_delegate grpcsharp_request_call_context_request_metadata;
        public readonly Delegates.grpcsharp_request_call_context_reset_delegate grpcsharp_request_call_context_reset;
        public readonly Delegates.grpcsharp_request_call_context_destroy_delegate grpcsharp_request_call_context_destroy;
        public readonly Delegates.grpcsharp_composite_call_credentials_create_delegate grpcsharp_composite_call_credentials_create;
        public readonly Delegates.grpcsharp_call_credentials_release_delegate grpcsharp_call_credentials_release;
        public readonly Delegates.grpcsharp_call_cancel_delegate grpcsharp_call_cancel;
        public readonly Delegates.grpcsharp_call_cancel_with_status_delegate grpcsharp_call_cancel_with_status;
        public readonly Delegates.grpcsharp_call_start_unary_delegate grpcsharp_call_start_unary;
        public readonly Delegates.grpcsharp_call_start_client_streaming_delegate grpcsharp_call_start_client_streaming;
        public readonly Delegates.grpcsharp_call_start_server_streaming_delegate grpcsharp_call_start_server_streaming;
        public readonly Delegates.grpcsharp_call_start_duplex_streaming_delegate grpcsharp_call_start_duplex_streaming;
        public readonly Delegates.grpcsharp_call_send_message_delegate grpcsharp_call_send_message;
        public readonly Delegates.grpcsharp_call_send_close_from_client_delegate grpcsharp_call_send_close_from_client;
        public readonly Delegates.grpcsharp_call_send_status_from_server_delegate grpcsharp_call_send_status_from_server;
        public readonly Delegates.grpcsharp_call_recv_message_delegate grpcsharp_call_recv_message;
        public readonly Delegates.grpcsharp_call_recv_initial_metadata_delegate grpcsharp_call_recv_initial_metadata;
        public readonly Delegates.grpcsharp_call_start_serverside_delegate grpcsharp_call_start_serverside;
        public readonly Delegates.grpcsharp_call_send_initial_metadata_delegate grpcsharp_call_send_initial_metadata;
        public readonly Delegates.grpcsharp_call_set_credentials_delegate grpcsharp_call_set_credentials;
        public readonly Delegates.grpcsharp_call_get_peer_delegate grpcsharp_call_get_peer;
        public readonly Delegates.grpcsharp_call_destroy_delegate grpcsharp_call_destroy;
        public readonly Delegates.grpcsharp_channel_args_create_delegate grpcsharp_channel_args_create;
        public readonly Delegates.grpcsharp_channel_args_set_string_delegate grpcsharp_channel_args_set_string;
        public readonly Delegates.grpcsharp_channel_args_set_integer_delegate grpcsharp_channel_args_set_integer;
        public readonly Delegates.grpcsharp_channel_args_destroy_delegate grpcsharp_channel_args_destroy;
        public readonly Delegates.grpcsharp_override_default_ssl_roots_delegate grpcsharp_override_default_ssl_roots;
        public readonly Delegates.grpcsharp_ssl_credentials_create_delegate grpcsharp_ssl_credentials_create;
        public readonly Delegates.grpcsharp_composite_channel_credentials_create_delegate grpcsharp_composite_channel_credentials_create;
        public readonly Delegates.grpcsharp_channel_credentials_release_delegate grpcsharp_channel_credentials_release;
        public readonly Delegates.grpcsharp_insecure_channel_create_delegate grpcsharp_insecure_channel_create;
        public readonly Delegates.grpcsharp_secure_channel_create_delegate grpcsharp_secure_channel_create;
        public readonly Delegates.grpcsharp_channel_create_call_delegate grpcsharp_channel_create_call;
        public readonly Delegates.grpcsharp_channel_check_connectivity_state_delegate grpcsharp_channel_check_connectivity_state;
        public readonly Delegates.grpcsharp_channel_watch_connectivity_state_delegate grpcsharp_channel_watch_connectivity_state;
        public readonly Delegates.grpcsharp_channel_get_target_delegate grpcsharp_channel_get_target;
        public readonly Delegates.grpcsharp_channel_destroy_delegate grpcsharp_channel_destroy;
        public readonly Delegates.grpcsharp_sizeof_grpc_event_delegate grpcsharp_sizeof_grpc_event;
        public readonly Delegates.grpcsharp_completion_queue_create_async_delegate grpcsharp_completion_queue_create_async;
        public readonly Delegates.grpcsharp_completion_queue_create_sync_delegate grpcsharp_completion_queue_create_sync;
        public readonly Delegates.grpcsharp_completion_queue_shutdown_delegate grpcsharp_completion_queue_shutdown;
        public readonly Delegates.grpcsharp_completion_queue_next_delegate grpcsharp_completion_queue_next;
        public readonly Delegates.grpcsharp_completion_queue_pluck_delegate grpcsharp_completion_queue_pluck;
        public readonly Delegates.grpcsharp_completion_queue_destroy_delegate grpcsharp_completion_queue_destroy;
        public readonly Delegates.gprsharp_free_delegate gprsharp_free;
        public readonly Delegates.grpcsharp_metadata_array_create_delegate grpcsharp_metadata_array_create;
        public readonly Delegates.grpcsharp_metadata_array_add_delegate grpcsharp_metadata_array_add;
        public readonly Delegates.grpcsharp_metadata_array_count_delegate grpcsharp_metadata_array_count;
        public readonly Delegates.grpcsharp_metadata_array_get_key_delegate grpcsharp_metadata_array_get_key;
        public readonly Delegates.grpcsharp_metadata_array_get_value_delegate grpcsharp_metadata_array_get_value;
        public readonly Delegates.grpcsharp_metadata_array_destroy_full_delegate grpcsharp_metadata_array_destroy_full;
        public readonly Delegates.grpcsharp_redirect_log_delegate grpcsharp_redirect_log;
        public readonly Delegates.grpcsharp_native_callback_dispatcher_init_delegate grpcsharp_native_callback_dispatcher_init;
        public readonly Delegates.grpcsharp_metadata_credentials_create_from_plugin_delegate grpcsharp_metadata_credentials_create_from_plugin;
        public readonly Delegates.grpcsharp_metadata_credentials_notify_from_plugin_delegate grpcsharp_metadata_credentials_notify_from_plugin;
        public readonly Delegates.grpcsharp_ssl_server_credentials_create_delegate grpcsharp_ssl_server_credentials_create;
        public readonly Delegates.grpcsharp_server_credentials_release_delegate grpcsharp_server_credentials_release;
        public readonly Delegates.grpcsharp_server_create_delegate grpcsharp_server_create;
        public readonly Delegates.grpcsharp_server_register_completion_queue_delegate grpcsharp_server_register_completion_queue;
        public readonly Delegates.grpcsharp_server_add_insecure_http2_port_delegate grpcsharp_server_add_insecure_http2_port;
        public readonly Delegates.grpcsharp_server_add_secure_http2_port_delegate grpcsharp_server_add_secure_http2_port;
        public readonly Delegates.grpcsharp_server_start_delegate grpcsharp_server_start;
        public readonly Delegates.grpcsharp_server_request_call_delegate grpcsharp_server_request_call;
        public readonly Delegates.grpcsharp_server_cancel_all_calls_delegate grpcsharp_server_cancel_all_calls;
        public readonly Delegates.grpcsharp_server_shutdown_and_notify_callback_delegate grpcsharp_server_shutdown_and_notify_callback;
        public readonly Delegates.grpcsharp_server_destroy_delegate grpcsharp_server_destroy;
        public readonly Delegates.grpcsharp_call_auth_context_delegate grpcsharp_call_auth_context;
        public readonly Delegates.grpcsharp_auth_context_peer_identity_property_name_delegate grpcsharp_auth_context_peer_identity_property_name;
        public readonly Delegates.grpcsharp_auth_context_property_iterator_delegate grpcsharp_auth_context_property_iterator;
        public readonly Delegates.grpcsharp_auth_property_iterator_next_delegate grpcsharp_auth_property_iterator_next;
        public readonly Delegates.grpcsharp_auth_context_release_delegate grpcsharp_auth_context_release;
        public readonly Delegates.grpcsharp_slice_buffer_create_delegate grpcsharp_slice_buffer_create;
        public readonly Delegates.grpcsharp_slice_buffer_adjust_tail_space_delegate grpcsharp_slice_buffer_adjust_tail_space;
        public readonly Delegates.grpcsharp_slice_buffer_slice_count_delegate grpcsharp_slice_buffer_slice_count;
        public readonly Delegates.grpcsharp_slice_buffer_slice_peek_delegate grpcsharp_slice_buffer_slice_peek;
        public readonly Delegates.grpcsharp_slice_buffer_reset_and_unref_delegate grpcsharp_slice_buffer_reset_and_unref;
        public readonly Delegates.grpcsharp_slice_buffer_destroy_delegate grpcsharp_slice_buffer_destroy;
        public readonly Delegates.gprsharp_now_delegate gprsharp_now;
        public readonly Delegates.gprsharp_inf_future_delegate gprsharp_inf_future;
        public readonly Delegates.gprsharp_inf_past_delegate gprsharp_inf_past;
        public readonly Delegates.gprsharp_convert_clock_type_delegate gprsharp_convert_clock_type;
        public readonly Delegates.gprsharp_sizeof_timespec_delegate gprsharp_sizeof_timespec;
        public readonly Delegates.grpcsharp_test_callback_delegate grpcsharp_test_callback;
        public readonly Delegates.grpcsharp_test_nop_delegate grpcsharp_test_nop;
        public readonly Delegates.grpcsharp_test_override_method_delegate grpcsharp_test_override_method;
        public readonly Delegates.grpcsharp_test_call_start_unary_echo_delegate grpcsharp_test_call_start_unary_echo;

        #endregion

        public NativeMethods(UnmanagedLibrary library)
        {
            this.grpcsharp_init = GetMethodDelegate<Delegates.grpcsharp_init_delegate>(library);
            this.grpcsharp_shutdown = GetMethodDelegate<Delegates.grpcsharp_shutdown_delegate>(library);
            this.grpcsharp_version_string = GetMethodDelegate<Delegates.grpcsharp_version_string_delegate>(library);
            this.grpcsharp_batch_context_create = GetMethodDelegate<Delegates.grpcsharp_batch_context_create_delegate>(library);
            this.grpcsharp_batch_context_recv_initial_metadata = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_initial_metadata_delegate>(library);
            this.grpcsharp_batch_context_recv_message_length = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_message_length_delegate>(library);
            this.grpcsharp_batch_context_recv_message_next_slice_peek = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_message_next_slice_peek_delegate>(library);
            this.grpcsharp_batch_context_recv_status_on_client_status = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_status_on_client_status_delegate>(library);
            this.grpcsharp_batch_context_recv_status_on_client_details = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_status_on_client_details_delegate>(library);
            this.grpcsharp_batch_context_recv_status_on_client_error_string = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_status_on_client_error_string_delegate>(library);
            this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_status_on_client_trailing_metadata_delegate>(library);
            this.grpcsharp_batch_context_recv_close_on_server_cancelled = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_close_on_server_cancelled_delegate>(library);
            this.grpcsharp_batch_context_reset = GetMethodDelegate<Delegates.grpcsharp_batch_context_reset_delegate>(library);
            this.grpcsharp_batch_context_destroy = GetMethodDelegate<Delegates.grpcsharp_batch_context_destroy_delegate>(library);
            this.grpcsharp_request_call_context_create = GetMethodDelegate<Delegates.grpcsharp_request_call_context_create_delegate>(library);
            this.grpcsharp_request_call_context_call = GetMethodDelegate<Delegates.grpcsharp_request_call_context_call_delegate>(library);
            this.grpcsharp_request_call_context_method = GetMethodDelegate<Delegates.grpcsharp_request_call_context_method_delegate>(library);
            this.grpcsharp_request_call_context_host = GetMethodDelegate<Delegates.grpcsharp_request_call_context_host_delegate>(library);
            this.grpcsharp_request_call_context_deadline = GetMethodDelegate<Delegates.grpcsharp_request_call_context_deadline_delegate>(library);
            this.grpcsharp_request_call_context_request_metadata = GetMethodDelegate<Delegates.grpcsharp_request_call_context_request_metadata_delegate>(library);
            this.grpcsharp_request_call_context_reset = GetMethodDelegate<Delegates.grpcsharp_request_call_context_reset_delegate>(library);
            this.grpcsharp_request_call_context_destroy = GetMethodDelegate<Delegates.grpcsharp_request_call_context_destroy_delegate>(library);
            this.grpcsharp_composite_call_credentials_create = GetMethodDelegate<Delegates.grpcsharp_composite_call_credentials_create_delegate>(library);
            this.grpcsharp_call_credentials_release = GetMethodDelegate<Delegates.grpcsharp_call_credentials_release_delegate>(library);
            this.grpcsharp_call_cancel = GetMethodDelegate<Delegates.grpcsharp_call_cancel_delegate>(library);
            this.grpcsharp_call_cancel_with_status = GetMethodDelegate<Delegates.grpcsharp_call_cancel_with_status_delegate>(library);
            this.grpcsharp_call_start_unary = GetMethodDelegate<Delegates.grpcsharp_call_start_unary_delegate>(library);
            this.grpcsharp_call_start_client_streaming = GetMethodDelegate<Delegates.grpcsharp_call_start_client_streaming_delegate>(library);
            this.grpcsharp_call_start_server_streaming = GetMethodDelegate<Delegates.grpcsharp_call_start_server_streaming_delegate>(library);
            this.grpcsharp_call_start_duplex_streaming = GetMethodDelegate<Delegates.grpcsharp_call_start_duplex_streaming_delegate>(library);
            this.grpcsharp_call_send_message = GetMethodDelegate<Delegates.grpcsharp_call_send_message_delegate>(library);
            this.grpcsharp_call_send_close_from_client = GetMethodDelegate<Delegates.grpcsharp_call_send_close_from_client_delegate>(library);
            this.grpcsharp_call_send_status_from_server = GetMethodDelegate<Delegates.grpcsharp_call_send_status_from_server_delegate>(library);
            this.grpcsharp_call_recv_message = GetMethodDelegate<Delegates.grpcsharp_call_recv_message_delegate>(library);
            this.grpcsharp_call_recv_initial_metadata = GetMethodDelegate<Delegates.grpcsharp_call_recv_initial_metadata_delegate>(library);
            this.grpcsharp_call_start_serverside = GetMethodDelegate<Delegates.grpcsharp_call_start_serverside_delegate>(library);
            this.grpcsharp_call_send_initial_metadata = GetMethodDelegate<Delegates.grpcsharp_call_send_initial_metadata_delegate>(library);
            this.grpcsharp_call_set_credentials = GetMethodDelegate<Delegates.grpcsharp_call_set_credentials_delegate>(library);
            this.grpcsharp_call_get_peer = GetMethodDelegate<Delegates.grpcsharp_call_get_peer_delegate>(library);
            this.grpcsharp_call_destroy = GetMethodDelegate<Delegates.grpcsharp_call_destroy_delegate>(library);
            this.grpcsharp_channel_args_create = GetMethodDelegate<Delegates.grpcsharp_channel_args_create_delegate>(library);
            this.grpcsharp_channel_args_set_string = GetMethodDelegate<Delegates.grpcsharp_channel_args_set_string_delegate>(library);
            this.grpcsharp_channel_args_set_integer = GetMethodDelegate<Delegates.grpcsharp_channel_args_set_integer_delegate>(library);
            this.grpcsharp_channel_args_destroy = GetMethodDelegate<Delegates.grpcsharp_channel_args_destroy_delegate>(library);
            this.grpcsharp_override_default_ssl_roots = GetMethodDelegate<Delegates.grpcsharp_override_default_ssl_roots_delegate>(library);
            this.grpcsharp_ssl_credentials_create = GetMethodDelegate<Delegates.grpcsharp_ssl_credentials_create_delegate>(library);
            this.grpcsharp_composite_channel_credentials_create = GetMethodDelegate<Delegates.grpcsharp_composite_channel_credentials_create_delegate>(library);
            this.grpcsharp_channel_credentials_release = GetMethodDelegate<Delegates.grpcsharp_channel_credentials_release_delegate>(library);
            this.grpcsharp_insecure_channel_create = GetMethodDelegate<Delegates.grpcsharp_insecure_channel_create_delegate>(library);
            this.grpcsharp_secure_channel_create = GetMethodDelegate<Delegates.grpcsharp_secure_channel_create_delegate>(library);
            this.grpcsharp_channel_create_call = GetMethodDelegate<Delegates.grpcsharp_channel_create_call_delegate>(library);
            this.grpcsharp_channel_check_connectivity_state = GetMethodDelegate<Delegates.grpcsharp_channel_check_connectivity_state_delegate>(library);
            this.grpcsharp_channel_watch_connectivity_state = GetMethodDelegate<Delegates.grpcsharp_channel_watch_connectivity_state_delegate>(library);
            this.grpcsharp_channel_get_target = GetMethodDelegate<Delegates.grpcsharp_channel_get_target_delegate>(library);
            this.grpcsharp_channel_destroy = GetMethodDelegate<Delegates.grpcsharp_channel_destroy_delegate>(library);
            this.grpcsharp_sizeof_grpc_event = GetMethodDelegate<Delegates.grpcsharp_sizeof_grpc_event_delegate>(library);
            this.grpcsharp_completion_queue_create_async = GetMethodDelegate<Delegates.grpcsharp_completion_queue_create_async_delegate>(library);
            this.grpcsharp_completion_queue_create_sync = GetMethodDelegate<Delegates.grpcsharp_completion_queue_create_sync_delegate>(library);
            this.grpcsharp_completion_queue_shutdown = GetMethodDelegate<Delegates.grpcsharp_completion_queue_shutdown_delegate>(library);
            this.grpcsharp_completion_queue_next = GetMethodDelegate<Delegates.grpcsharp_completion_queue_next_delegate>(library);
            this.grpcsharp_completion_queue_pluck = GetMethodDelegate<Delegates.grpcsharp_completion_queue_pluck_delegate>(library);
            this.grpcsharp_completion_queue_destroy = GetMethodDelegate<Delegates.grpcsharp_completion_queue_destroy_delegate>(library);
            this.gprsharp_free = GetMethodDelegate<Delegates.gprsharp_free_delegate>(library);
            this.grpcsharp_metadata_array_create = GetMethodDelegate<Delegates.grpcsharp_metadata_array_create_delegate>(library);
            this.grpcsharp_metadata_array_add = GetMethodDelegate<Delegates.grpcsharp_metadata_array_add_delegate>(library);
            this.grpcsharp_metadata_array_count = GetMethodDelegate<Delegates.grpcsharp_metadata_array_count_delegate>(library);
            this.grpcsharp_metadata_array_get_key = GetMethodDelegate<Delegates.grpcsharp_metadata_array_get_key_delegate>(library);
            this.grpcsharp_metadata_array_get_value = GetMethodDelegate<Delegates.grpcsharp_metadata_array_get_value_delegate>(library);
            this.grpcsharp_metadata_array_destroy_full = GetMethodDelegate<Delegates.grpcsharp_metadata_array_destroy_full_delegate>(library);
            this.grpcsharp_redirect_log = GetMethodDelegate<Delegates.grpcsharp_redirect_log_delegate>(library);
            this.grpcsharp_native_callback_dispatcher_init = GetMethodDelegate<Delegates.grpcsharp_native_callback_dispatcher_init_delegate>(library);
            this.grpcsharp_metadata_credentials_create_from_plugin = GetMethodDelegate<Delegates.grpcsharp_metadata_credentials_create_from_plugin_delegate>(library);
            this.grpcsharp_metadata_credentials_notify_from_plugin = GetMethodDelegate<Delegates.grpcsharp_metadata_credentials_notify_from_plugin_delegate>(library);
            this.grpcsharp_ssl_server_credentials_create = GetMethodDelegate<Delegates.grpcsharp_ssl_server_credentials_create_delegate>(library);
            this.grpcsharp_server_credentials_release = GetMethodDelegate<Delegates.grpcsharp_server_credentials_release_delegate>(library);
            this.grpcsharp_server_create = GetMethodDelegate<Delegates.grpcsharp_server_create_delegate>(library);
            this.grpcsharp_server_register_completion_queue = GetMethodDelegate<Delegates.grpcsharp_server_register_completion_queue_delegate>(library);
            this.grpcsharp_server_add_insecure_http2_port = GetMethodDelegate<Delegates.grpcsharp_server_add_insecure_http2_port_delegate>(library);
            this.grpcsharp_server_add_secure_http2_port = GetMethodDelegate<Delegates.grpcsharp_server_add_secure_http2_port_delegate>(library);
            this.grpcsharp_server_start = GetMethodDelegate<Delegates.grpcsharp_server_start_delegate>(library);
            this.grpcsharp_server_request_call = GetMethodDelegate<Delegates.grpcsharp_server_request_call_delegate>(library);
            this.grpcsharp_server_cancel_all_calls = GetMethodDelegate<Delegates.grpcsharp_server_cancel_all_calls_delegate>(library);
            this.grpcsharp_server_shutdown_and_notify_callback = GetMethodDelegate<Delegates.grpcsharp_server_shutdown_and_notify_callback_delegate>(library);
            this.grpcsharp_server_destroy = GetMethodDelegate<Delegates.grpcsharp_server_destroy_delegate>(library);
            this.grpcsharp_call_auth_context = GetMethodDelegate<Delegates.grpcsharp_call_auth_context_delegate>(library);
            this.grpcsharp_auth_context_peer_identity_property_name = GetMethodDelegate<Delegates.grpcsharp_auth_context_peer_identity_property_name_delegate>(library);
            this.grpcsharp_auth_context_property_iterator = GetMethodDelegate<Delegates.grpcsharp_auth_context_property_iterator_delegate>(library);
            this.grpcsharp_auth_property_iterator_next = GetMethodDelegate<Delegates.grpcsharp_auth_property_iterator_next_delegate>(library);
            this.grpcsharp_auth_context_release = GetMethodDelegate<Delegates.grpcsharp_auth_context_release_delegate>(library);
            this.grpcsharp_slice_buffer_create = GetMethodDelegate<Delegates.grpcsharp_slice_buffer_create_delegate>(library);
            this.grpcsharp_slice_buffer_adjust_tail_space = GetMethodDelegate<Delegates.grpcsharp_slice_buffer_adjust_tail_space_delegate>(library);
            this.grpcsharp_slice_buffer_slice_count = GetMethodDelegate<Delegates.grpcsharp_slice_buffer_slice_count_delegate>(library);
            this.grpcsharp_slice_buffer_slice_peek = GetMethodDelegate<Delegates.grpcsharp_slice_buffer_slice_peek_delegate>(library);
            this.grpcsharp_slice_buffer_reset_and_unref = GetMethodDelegate<Delegates.grpcsharp_slice_buffer_reset_and_unref_delegate>(library);
            this.grpcsharp_slice_buffer_destroy = GetMethodDelegate<Delegates.grpcsharp_slice_buffer_destroy_delegate>(library);
            this.gprsharp_now = GetMethodDelegate<Delegates.gprsharp_now_delegate>(library);
            this.gprsharp_inf_future = GetMethodDelegate<Delegates.gprsharp_inf_future_delegate>(library);
            this.gprsharp_inf_past = GetMethodDelegate<Delegates.gprsharp_inf_past_delegate>(library);
            this.gprsharp_convert_clock_type = GetMethodDelegate<Delegates.gprsharp_convert_clock_type_delegate>(library);
            this.gprsharp_sizeof_timespec = GetMethodDelegate<Delegates.gprsharp_sizeof_timespec_delegate>(library);
            this.grpcsharp_test_callback = GetMethodDelegate<Delegates.grpcsharp_test_callback_delegate>(library);
            this.grpcsharp_test_nop = GetMethodDelegate<Delegates.grpcsharp_test_nop_delegate>(library);
            this.grpcsharp_test_override_method = GetMethodDelegate<Delegates.grpcsharp_test_override_method_delegate>(library);
            this.grpcsharp_test_call_start_unary_echo = GetMethodDelegate<Delegates.grpcsharp_test_call_start_unary_echo_delegate>(library);
        }
        
        public NativeMethods(DllImportsFromStaticLib unusedInstance)
        {
            this.grpcsharp_init = DllImportsFromStaticLib.grpcsharp_init;
            this.grpcsharp_shutdown = DllImportsFromStaticLib.grpcsharp_shutdown;
            this.grpcsharp_version_string = DllImportsFromStaticLib.grpcsharp_version_string;
            this.grpcsharp_batch_context_create = DllImportsFromStaticLib.grpcsharp_batch_context_create;
            this.grpcsharp_batch_context_recv_initial_metadata = DllImportsFromStaticLib.grpcsharp_batch_context_recv_initial_metadata;
            this.grpcsharp_batch_context_recv_message_length = DllImportsFromStaticLib.grpcsharp_batch_context_recv_message_length;
            this.grpcsharp_batch_context_recv_message_next_slice_peek = DllImportsFromStaticLib.grpcsharp_batch_context_recv_message_next_slice_peek;
            this.grpcsharp_batch_context_recv_status_on_client_status = DllImportsFromStaticLib.grpcsharp_batch_context_recv_status_on_client_status;
            this.grpcsharp_batch_context_recv_status_on_client_details = DllImportsFromStaticLib.grpcsharp_batch_context_recv_status_on_client_details;
            this.grpcsharp_batch_context_recv_status_on_client_error_string = DllImportsFromStaticLib.grpcsharp_batch_context_recv_status_on_client_error_string;
            this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = DllImportsFromStaticLib.grpcsharp_batch_context_recv_status_on_client_trailing_metadata;
            this.grpcsharp_batch_context_recv_close_on_server_cancelled = DllImportsFromStaticLib.grpcsharp_batch_context_recv_close_on_server_cancelled;
            this.grpcsharp_batch_context_reset = DllImportsFromStaticLib.grpcsharp_batch_context_reset;
            this.grpcsharp_batch_context_destroy = DllImportsFromStaticLib.grpcsharp_batch_context_destroy;
            this.grpcsharp_request_call_context_create = DllImportsFromStaticLib.grpcsharp_request_call_context_create;
            this.grpcsharp_request_call_context_call = DllImportsFromStaticLib.grpcsharp_request_call_context_call;
            this.grpcsharp_request_call_context_method = DllImportsFromStaticLib.grpcsharp_request_call_context_method;
            this.grpcsharp_request_call_context_host = DllImportsFromStaticLib.grpcsharp_request_call_context_host;
            this.grpcsharp_request_call_context_deadline = DllImportsFromStaticLib.grpcsharp_request_call_context_deadline;
            this.grpcsharp_request_call_context_request_metadata = DllImportsFromStaticLib.grpcsharp_request_call_context_request_metadata;
            this.grpcsharp_request_call_context_reset = DllImportsFromStaticLib.grpcsharp_request_call_context_reset;
            this.grpcsharp_request_call_context_destroy = DllImportsFromStaticLib.grpcsharp_request_call_context_destroy;
            this.grpcsharp_composite_call_credentials_create = DllImportsFromStaticLib.grpcsharp_composite_call_credentials_create;
            this.grpcsharp_call_credentials_release = DllImportsFromStaticLib.grpcsharp_call_credentials_release;
            this.grpcsharp_call_cancel = DllImportsFromStaticLib.grpcsharp_call_cancel;
            this.grpcsharp_call_cancel_with_status = DllImportsFromStaticLib.grpcsharp_call_cancel_with_status;
            this.grpcsharp_call_start_unary = DllImportsFromStaticLib.grpcsharp_call_start_unary;
            this.grpcsharp_call_start_client_streaming = DllImportsFromStaticLib.grpcsharp_call_start_client_streaming;
            this.grpcsharp_call_start_server_streaming = DllImportsFromStaticLib.grpcsharp_call_start_server_streaming;
            this.grpcsharp_call_start_duplex_streaming = DllImportsFromStaticLib.grpcsharp_call_start_duplex_streaming;
            this.grpcsharp_call_send_message = DllImportsFromStaticLib.grpcsharp_call_send_message;
            this.grpcsharp_call_send_close_from_client = DllImportsFromStaticLib.grpcsharp_call_send_close_from_client;
            this.grpcsharp_call_send_status_from_server = DllImportsFromStaticLib.grpcsharp_call_send_status_from_server;
            this.grpcsharp_call_recv_message = DllImportsFromStaticLib.grpcsharp_call_recv_message;
            this.grpcsharp_call_recv_initial_metadata = DllImportsFromStaticLib.grpcsharp_call_recv_initial_metadata;
            this.grpcsharp_call_start_serverside = DllImportsFromStaticLib.grpcsharp_call_start_serverside;
            this.grpcsharp_call_send_initial_metadata = DllImportsFromStaticLib.grpcsharp_call_send_initial_metadata;
            this.grpcsharp_call_set_credentials = DllImportsFromStaticLib.grpcsharp_call_set_credentials;
            this.grpcsharp_call_get_peer = DllImportsFromStaticLib.grpcsharp_call_get_peer;
            this.grpcsharp_call_destroy = DllImportsFromStaticLib.grpcsharp_call_destroy;
            this.grpcsharp_channel_args_create = DllImportsFromStaticLib.grpcsharp_channel_args_create;
            this.grpcsharp_channel_args_set_string = DllImportsFromStaticLib.grpcsharp_channel_args_set_string;
            this.grpcsharp_channel_args_set_integer = DllImportsFromStaticLib.grpcsharp_channel_args_set_integer;
            this.grpcsharp_channel_args_destroy = DllImportsFromStaticLib.grpcsharp_channel_args_destroy;
            this.grpcsharp_override_default_ssl_roots = DllImportsFromStaticLib.grpcsharp_override_default_ssl_roots;
            this.grpcsharp_ssl_credentials_create = DllImportsFromStaticLib.grpcsharp_ssl_credentials_create;
            this.grpcsharp_composite_channel_credentials_create = DllImportsFromStaticLib.grpcsharp_composite_channel_credentials_create;
            this.grpcsharp_channel_credentials_release = DllImportsFromStaticLib.grpcsharp_channel_credentials_release;
            this.grpcsharp_insecure_channel_create = DllImportsFromStaticLib.grpcsharp_insecure_channel_create;
            this.grpcsharp_secure_channel_create = DllImportsFromStaticLib.grpcsharp_secure_channel_create;
            this.grpcsharp_channel_create_call = DllImportsFromStaticLib.grpcsharp_channel_create_call;
            this.grpcsharp_channel_check_connectivity_state = DllImportsFromStaticLib.grpcsharp_channel_check_connectivity_state;
            this.grpcsharp_channel_watch_connectivity_state = DllImportsFromStaticLib.grpcsharp_channel_watch_connectivity_state;
            this.grpcsharp_channel_get_target = DllImportsFromStaticLib.grpcsharp_channel_get_target;
            this.grpcsharp_channel_destroy = DllImportsFromStaticLib.grpcsharp_channel_destroy;
            this.grpcsharp_sizeof_grpc_event = DllImportsFromStaticLib.grpcsharp_sizeof_grpc_event;
            this.grpcsharp_completion_queue_create_async = DllImportsFromStaticLib.grpcsharp_completion_queue_create_async;
            this.grpcsharp_completion_queue_create_sync = DllImportsFromStaticLib.grpcsharp_completion_queue_create_sync;
            this.grpcsharp_completion_queue_shutdown = DllImportsFromStaticLib.grpcsharp_completion_queue_shutdown;
            this.grpcsharp_completion_queue_next = DllImportsFromStaticLib.grpcsharp_completion_queue_next;
            this.grpcsharp_completion_queue_pluck = DllImportsFromStaticLib.grpcsharp_completion_queue_pluck;
            this.grpcsharp_completion_queue_destroy = DllImportsFromStaticLib.grpcsharp_completion_queue_destroy;
            this.gprsharp_free = DllImportsFromStaticLib.gprsharp_free;
            this.grpcsharp_metadata_array_create = DllImportsFromStaticLib.grpcsharp_metadata_array_create;
            this.grpcsharp_metadata_array_add = DllImportsFromStaticLib.grpcsharp_metadata_array_add;
            this.grpcsharp_metadata_array_count = DllImportsFromStaticLib.grpcsharp_metadata_array_count;
            this.grpcsharp_metadata_array_get_key = DllImportsFromStaticLib.grpcsharp_metadata_array_get_key;
            this.grpcsharp_metadata_array_get_value = DllImportsFromStaticLib.grpcsharp_metadata_array_get_value;
            this.grpcsharp_metadata_array_destroy_full = DllImportsFromStaticLib.grpcsharp_metadata_array_destroy_full;
            this.grpcsharp_redirect_log = DllImportsFromStaticLib.grpcsharp_redirect_log;
            this.grpcsharp_native_callback_dispatcher_init = DllImportsFromStaticLib.grpcsharp_native_callback_dispatcher_init;
            this.grpcsharp_metadata_credentials_create_from_plugin = DllImportsFromStaticLib.grpcsharp_metadata_credentials_create_from_plugin;
            this.grpcsharp_metadata_credentials_notify_from_plugin = DllImportsFromStaticLib.grpcsharp_metadata_credentials_notify_from_plugin;
            this.grpcsharp_ssl_server_credentials_create = DllImportsFromStaticLib.grpcsharp_ssl_server_credentials_create;
            this.grpcsharp_server_credentials_release = DllImportsFromStaticLib.grpcsharp_server_credentials_release;
            this.grpcsharp_server_create = DllImportsFromStaticLib.grpcsharp_server_create;
            this.grpcsharp_server_register_completion_queue = DllImportsFromStaticLib.grpcsharp_server_register_completion_queue;
            this.grpcsharp_server_add_insecure_http2_port = DllImportsFromStaticLib.grpcsharp_server_add_insecure_http2_port;
            this.grpcsharp_server_add_secure_http2_port = DllImportsFromStaticLib.grpcsharp_server_add_secure_http2_port;
            this.grpcsharp_server_start = DllImportsFromStaticLib.grpcsharp_server_start;
            this.grpcsharp_server_request_call = DllImportsFromStaticLib.grpcsharp_server_request_call;
            this.grpcsharp_server_cancel_all_calls = DllImportsFromStaticLib.grpcsharp_server_cancel_all_calls;
            this.grpcsharp_server_shutdown_and_notify_callback = DllImportsFromStaticLib.grpcsharp_server_shutdown_and_notify_callback;
            this.grpcsharp_server_destroy = DllImportsFromStaticLib.grpcsharp_server_destroy;
            this.grpcsharp_call_auth_context = DllImportsFromStaticLib.grpcsharp_call_auth_context;
            this.grpcsharp_auth_context_peer_identity_property_name = DllImportsFromStaticLib.grpcsharp_auth_context_peer_identity_property_name;
            this.grpcsharp_auth_context_property_iterator = DllImportsFromStaticLib.grpcsharp_auth_context_property_iterator;
            this.grpcsharp_auth_property_iterator_next = DllImportsFromStaticLib.grpcsharp_auth_property_iterator_next;
            this.grpcsharp_auth_context_release = DllImportsFromStaticLib.grpcsharp_auth_context_release;
            this.grpcsharp_slice_buffer_create = DllImportsFromStaticLib.grpcsharp_slice_buffer_create;
            this.grpcsharp_slice_buffer_adjust_tail_space = DllImportsFromStaticLib.grpcsharp_slice_buffer_adjust_tail_space;
            this.grpcsharp_slice_buffer_slice_count = DllImportsFromStaticLib.grpcsharp_slice_buffer_slice_count;
            this.grpcsharp_slice_buffer_slice_peek = DllImportsFromStaticLib.grpcsharp_slice_buffer_slice_peek;
            this.grpcsharp_slice_buffer_reset_and_unref = DllImportsFromStaticLib.grpcsharp_slice_buffer_reset_and_unref;
            this.grpcsharp_slice_buffer_destroy = DllImportsFromStaticLib.grpcsharp_slice_buffer_destroy;
            this.gprsharp_now = DllImportsFromStaticLib.gprsharp_now;
            this.gprsharp_inf_future = DllImportsFromStaticLib.gprsharp_inf_future;
            this.gprsharp_inf_past = DllImportsFromStaticLib.gprsharp_inf_past;
            this.gprsharp_convert_clock_type = DllImportsFromStaticLib.gprsharp_convert_clock_type;
            this.gprsharp_sizeof_timespec = DllImportsFromStaticLib.gprsharp_sizeof_timespec;
            this.grpcsharp_test_callback = DllImportsFromStaticLib.grpcsharp_test_callback;
            this.grpcsharp_test_nop = DllImportsFromStaticLib.grpcsharp_test_nop;
            this.grpcsharp_test_override_method = DllImportsFromStaticLib.grpcsharp_test_override_method;
            this.grpcsharp_test_call_start_unary_echo = DllImportsFromStaticLib.grpcsharp_test_call_start_unary_echo;
        }
        
        public NativeMethods(DllImportsFromSharedLib unusedInstance)
        {
            this.grpcsharp_init = DllImportsFromSharedLib.grpcsharp_init;
            this.grpcsharp_shutdown = DllImportsFromSharedLib.grpcsharp_shutdown;
            this.grpcsharp_version_string = DllImportsFromSharedLib.grpcsharp_version_string;
            this.grpcsharp_batch_context_create = DllImportsFromSharedLib.grpcsharp_batch_context_create;
            this.grpcsharp_batch_context_recv_initial_metadata = DllImportsFromSharedLib.grpcsharp_batch_context_recv_initial_metadata;
            this.grpcsharp_batch_context_recv_message_length = DllImportsFromSharedLib.grpcsharp_batch_context_recv_message_length;
            this.grpcsharp_batch_context_recv_message_next_slice_peek = DllImportsFromSharedLib.grpcsharp_batch_context_recv_message_next_slice_peek;
            this.grpcsharp_batch_context_recv_status_on_client_status = DllImportsFromSharedLib.grpcsharp_batch_context_recv_status_on_client_status;
            this.grpcsharp_batch_context_recv_status_on_client_details = DllImportsFromSharedLib.grpcsharp_batch_context_recv_status_on_client_details;
            this.grpcsharp_batch_context_recv_status_on_client_error_string = DllImportsFromSharedLib.grpcsharp_batch_context_recv_status_on_client_error_string;
            this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = DllImportsFromSharedLib.grpcsharp_batch_context_recv_status_on_client_trailing_metadata;
            this.grpcsharp_batch_context_recv_close_on_server_cancelled = DllImportsFromSharedLib.grpcsharp_batch_context_recv_close_on_server_cancelled;
            this.grpcsharp_batch_context_reset = DllImportsFromSharedLib.grpcsharp_batch_context_reset;
            this.grpcsharp_batch_context_destroy = DllImportsFromSharedLib.grpcsharp_batch_context_destroy;
            this.grpcsharp_request_call_context_create = DllImportsFromSharedLib.grpcsharp_request_call_context_create;
            this.grpcsharp_request_call_context_call = DllImportsFromSharedLib.grpcsharp_request_call_context_call;
            this.grpcsharp_request_call_context_method = DllImportsFromSharedLib.grpcsharp_request_call_context_method;
            this.grpcsharp_request_call_context_host = DllImportsFromSharedLib.grpcsharp_request_call_context_host;
            this.grpcsharp_request_call_context_deadline = DllImportsFromSharedLib.grpcsharp_request_call_context_deadline;
            this.grpcsharp_request_call_context_request_metadata = DllImportsFromSharedLib.grpcsharp_request_call_context_request_metadata;
            this.grpcsharp_request_call_context_reset = DllImportsFromSharedLib.grpcsharp_request_call_context_reset;
            this.grpcsharp_request_call_context_destroy = DllImportsFromSharedLib.grpcsharp_request_call_context_destroy;
            this.grpcsharp_composite_call_credentials_create = DllImportsFromSharedLib.grpcsharp_composite_call_credentials_create;
            this.grpcsharp_call_credentials_release = DllImportsFromSharedLib.grpcsharp_call_credentials_release;
            this.grpcsharp_call_cancel = DllImportsFromSharedLib.grpcsharp_call_cancel;
            this.grpcsharp_call_cancel_with_status = DllImportsFromSharedLib.grpcsharp_call_cancel_with_status;
            this.grpcsharp_call_start_unary = DllImportsFromSharedLib.grpcsharp_call_start_unary;
            this.grpcsharp_call_start_client_streaming = DllImportsFromSharedLib.grpcsharp_call_start_client_streaming;
            this.grpcsharp_call_start_server_streaming = DllImportsFromSharedLib.grpcsharp_call_start_server_streaming;
            this.grpcsharp_call_start_duplex_streaming = DllImportsFromSharedLib.grpcsharp_call_start_duplex_streaming;
            this.grpcsharp_call_send_message = DllImportsFromSharedLib.grpcsharp_call_send_message;
            this.grpcsharp_call_send_close_from_client = DllImportsFromSharedLib.grpcsharp_call_send_close_from_client;
            this.grpcsharp_call_send_status_from_server = DllImportsFromSharedLib.grpcsharp_call_send_status_from_server;
            this.grpcsharp_call_recv_message = DllImportsFromSharedLib.grpcsharp_call_recv_message;
            this.grpcsharp_call_recv_initial_metadata = DllImportsFromSharedLib.grpcsharp_call_recv_initial_metadata;
            this.grpcsharp_call_start_serverside = DllImportsFromSharedLib.grpcsharp_call_start_serverside;
            this.grpcsharp_call_send_initial_metadata = DllImportsFromSharedLib.grpcsharp_call_send_initial_metadata;
            this.grpcsharp_call_set_credentials = DllImportsFromSharedLib.grpcsharp_call_set_credentials;
            this.grpcsharp_call_get_peer = DllImportsFromSharedLib.grpcsharp_call_get_peer;
            this.grpcsharp_call_destroy = DllImportsFromSharedLib.grpcsharp_call_destroy;
            this.grpcsharp_channel_args_create = DllImportsFromSharedLib.grpcsharp_channel_args_create;
            this.grpcsharp_channel_args_set_string = DllImportsFromSharedLib.grpcsharp_channel_args_set_string;
            this.grpcsharp_channel_args_set_integer = DllImportsFromSharedLib.grpcsharp_channel_args_set_integer;
            this.grpcsharp_channel_args_destroy = DllImportsFromSharedLib.grpcsharp_channel_args_destroy;
            this.grpcsharp_override_default_ssl_roots = DllImportsFromSharedLib.grpcsharp_override_default_ssl_roots;
            this.grpcsharp_ssl_credentials_create = DllImportsFromSharedLib.grpcsharp_ssl_credentials_create;
            this.grpcsharp_composite_channel_credentials_create = DllImportsFromSharedLib.grpcsharp_composite_channel_credentials_create;
            this.grpcsharp_channel_credentials_release = DllImportsFromSharedLib.grpcsharp_channel_credentials_release;
            this.grpcsharp_insecure_channel_create = DllImportsFromSharedLib.grpcsharp_insecure_channel_create;
            this.grpcsharp_secure_channel_create = DllImportsFromSharedLib.grpcsharp_secure_channel_create;
            this.grpcsharp_channel_create_call = DllImportsFromSharedLib.grpcsharp_channel_create_call;
            this.grpcsharp_channel_check_connectivity_state = DllImportsFromSharedLib.grpcsharp_channel_check_connectivity_state;
            this.grpcsharp_channel_watch_connectivity_state = DllImportsFromSharedLib.grpcsharp_channel_watch_connectivity_state;
            this.grpcsharp_channel_get_target = DllImportsFromSharedLib.grpcsharp_channel_get_target;
            this.grpcsharp_channel_destroy = DllImportsFromSharedLib.grpcsharp_channel_destroy;
            this.grpcsharp_sizeof_grpc_event = DllImportsFromSharedLib.grpcsharp_sizeof_grpc_event;
            this.grpcsharp_completion_queue_create_async = DllImportsFromSharedLib.grpcsharp_completion_queue_create_async;
            this.grpcsharp_completion_queue_create_sync = DllImportsFromSharedLib.grpcsharp_completion_queue_create_sync;
            this.grpcsharp_completion_queue_shutdown = DllImportsFromSharedLib.grpcsharp_completion_queue_shutdown;
            this.grpcsharp_completion_queue_next = DllImportsFromSharedLib.grpcsharp_completion_queue_next;
            this.grpcsharp_completion_queue_pluck = DllImportsFromSharedLib.grpcsharp_completion_queue_pluck;
            this.grpcsharp_completion_queue_destroy = DllImportsFromSharedLib.grpcsharp_completion_queue_destroy;
            this.gprsharp_free = DllImportsFromSharedLib.gprsharp_free;
            this.grpcsharp_metadata_array_create = DllImportsFromSharedLib.grpcsharp_metadata_array_create;
            this.grpcsharp_metadata_array_add = DllImportsFromSharedLib.grpcsharp_metadata_array_add;
            this.grpcsharp_metadata_array_count = DllImportsFromSharedLib.grpcsharp_metadata_array_count;
            this.grpcsharp_metadata_array_get_key = DllImportsFromSharedLib.grpcsharp_metadata_array_get_key;
            this.grpcsharp_metadata_array_get_value = DllImportsFromSharedLib.grpcsharp_metadata_array_get_value;
            this.grpcsharp_metadata_array_destroy_full = DllImportsFromSharedLib.grpcsharp_metadata_array_destroy_full;
            this.grpcsharp_redirect_log = DllImportsFromSharedLib.grpcsharp_redirect_log;
            this.grpcsharp_native_callback_dispatcher_init = DllImportsFromSharedLib.grpcsharp_native_callback_dispatcher_init;
            this.grpcsharp_metadata_credentials_create_from_plugin = DllImportsFromSharedLib.grpcsharp_metadata_credentials_create_from_plugin;
            this.grpcsharp_metadata_credentials_notify_from_plugin = DllImportsFromSharedLib.grpcsharp_metadata_credentials_notify_from_plugin;
            this.grpcsharp_ssl_server_credentials_create = DllImportsFromSharedLib.grpcsharp_ssl_server_credentials_create;
            this.grpcsharp_server_credentials_release = DllImportsFromSharedLib.grpcsharp_server_credentials_release;
            this.grpcsharp_server_create = DllImportsFromSharedLib.grpcsharp_server_create;
            this.grpcsharp_server_register_completion_queue = DllImportsFromSharedLib.grpcsharp_server_register_completion_queue;
            this.grpcsharp_server_add_insecure_http2_port = DllImportsFromSharedLib.grpcsharp_server_add_insecure_http2_port;
            this.grpcsharp_server_add_secure_http2_port = DllImportsFromSharedLib.grpcsharp_server_add_secure_http2_port;
            this.grpcsharp_server_start = DllImportsFromSharedLib.grpcsharp_server_start;
            this.grpcsharp_server_request_call = DllImportsFromSharedLib.grpcsharp_server_request_call;
            this.grpcsharp_server_cancel_all_calls = DllImportsFromSharedLib.grpcsharp_server_cancel_all_calls;
            this.grpcsharp_server_shutdown_and_notify_callback = DllImportsFromSharedLib.grpcsharp_server_shutdown_and_notify_callback;
            this.grpcsharp_server_destroy = DllImportsFromSharedLib.grpcsharp_server_destroy;
            this.grpcsharp_call_auth_context = DllImportsFromSharedLib.grpcsharp_call_auth_context;
            this.grpcsharp_auth_context_peer_identity_property_name = DllImportsFromSharedLib.grpcsharp_auth_context_peer_identity_property_name;
            this.grpcsharp_auth_context_property_iterator = DllImportsFromSharedLib.grpcsharp_auth_context_property_iterator;
            this.grpcsharp_auth_property_iterator_next = DllImportsFromSharedLib.grpcsharp_auth_property_iterator_next;
            this.grpcsharp_auth_context_release = DllImportsFromSharedLib.grpcsharp_auth_context_release;
            this.grpcsharp_slice_buffer_create = DllImportsFromSharedLib.grpcsharp_slice_buffer_create;
            this.grpcsharp_slice_buffer_adjust_tail_space = DllImportsFromSharedLib.grpcsharp_slice_buffer_adjust_tail_space;
            this.grpcsharp_slice_buffer_slice_count = DllImportsFromSharedLib.grpcsharp_slice_buffer_slice_count;
            this.grpcsharp_slice_buffer_slice_peek = DllImportsFromSharedLib.grpcsharp_slice_buffer_slice_peek;
            this.grpcsharp_slice_buffer_reset_and_unref = DllImportsFromSharedLib.grpcsharp_slice_buffer_reset_and_unref;
            this.grpcsharp_slice_buffer_destroy = DllImportsFromSharedLib.grpcsharp_slice_buffer_destroy;
            this.gprsharp_now = DllImportsFromSharedLib.gprsharp_now;
            this.gprsharp_inf_future = DllImportsFromSharedLib.gprsharp_inf_future;
            this.gprsharp_inf_past = DllImportsFromSharedLib.gprsharp_inf_past;
            this.gprsharp_convert_clock_type = DllImportsFromSharedLib.gprsharp_convert_clock_type;
            this.gprsharp_sizeof_timespec = DllImportsFromSharedLib.gprsharp_sizeof_timespec;
            this.grpcsharp_test_callback = DllImportsFromSharedLib.grpcsharp_test_callback;
            this.grpcsharp_test_nop = DllImportsFromSharedLib.grpcsharp_test_nop;
            this.grpcsharp_test_override_method = DllImportsFromSharedLib.grpcsharp_test_override_method;
            this.grpcsharp_test_call_start_unary_echo = DllImportsFromSharedLib.grpcsharp_test_call_start_unary_echo;
        }

        public NativeMethods(DllImportsFromSharedLib_x86 unusedInstance)
        {
            this.grpcsharp_init = DllImportsFromSharedLib_x86.grpcsharp_init;
            this.grpcsharp_shutdown = DllImportsFromSharedLib_x86.grpcsharp_shutdown;
            this.grpcsharp_version_string = DllImportsFromSharedLib_x86.grpcsharp_version_string;
            this.grpcsharp_batch_context_create = DllImportsFromSharedLib_x86.grpcsharp_batch_context_create;
            this.grpcsharp_batch_context_recv_initial_metadata = DllImportsFromSharedLib_x86.grpcsharp_batch_context_recv_initial_metadata;
            this.grpcsharp_batch_context_recv_message_length = DllImportsFromSharedLib_x86.grpcsharp_batch_context_recv_message_length;
            this.grpcsharp_batch_context_recv_message_next_slice_peek = DllImportsFromSharedLib_x86.grpcsharp_batch_context_recv_message_next_slice_peek;
            this.grpcsharp_batch_context_recv_status_on_client_status = DllImportsFromSharedLib_x86.grpcsharp_batch_context_recv_status_on_client_status;
            this.grpcsharp_batch_context_recv_status_on_client_details = DllImportsFromSharedLib_x86.grpcsharp_batch_context_recv_status_on_client_details;
            this.grpcsharp_batch_context_recv_status_on_client_error_string = DllImportsFromSharedLib_x86.grpcsharp_batch_context_recv_status_on_client_error_string;
            this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = DllImportsFromSharedLib_x86.grpcsharp_batch_context_recv_status_on_client_trailing_metadata;
            this.grpcsharp_batch_context_recv_close_on_server_cancelled = DllImportsFromSharedLib_x86.grpcsharp_batch_context_recv_close_on_server_cancelled;
            this.grpcsharp_batch_context_reset = DllImportsFromSharedLib_x86.grpcsharp_batch_context_reset;
            this.grpcsharp_batch_context_destroy = DllImportsFromSharedLib_x86.grpcsharp_batch_context_destroy;
            this.grpcsharp_request_call_context_create = DllImportsFromSharedLib_x86.grpcsharp_request_call_context_create;
            this.grpcsharp_request_call_context_call = DllImportsFromSharedLib_x86.grpcsharp_request_call_context_call;
            this.grpcsharp_request_call_context_method = DllImportsFromSharedLib_x86.grpcsharp_request_call_context_method;
            this.grpcsharp_request_call_context_host = DllImportsFromSharedLib_x86.grpcsharp_request_call_context_host;
            this.grpcsharp_request_call_context_deadline = DllImportsFromSharedLib_x86.grpcsharp_request_call_context_deadline;
            this.grpcsharp_request_call_context_request_metadata = DllImportsFromSharedLib_x86.grpcsharp_request_call_context_request_metadata;
            this.grpcsharp_request_call_context_reset = DllImportsFromSharedLib_x86.grpcsharp_request_call_context_reset;
            this.grpcsharp_request_call_context_destroy = DllImportsFromSharedLib_x86.grpcsharp_request_call_context_destroy;
            this.grpcsharp_composite_call_credentials_create = DllImportsFromSharedLib_x86.grpcsharp_composite_call_credentials_create;
            this.grpcsharp_call_credentials_release = DllImportsFromSharedLib_x86.grpcsharp_call_credentials_release;
            this.grpcsharp_call_cancel = DllImportsFromSharedLib_x86.grpcsharp_call_cancel;
            this.grpcsharp_call_cancel_with_status = DllImportsFromSharedLib_x86.grpcsharp_call_cancel_with_status;
            this.grpcsharp_call_start_unary = DllImportsFromSharedLib_x86.grpcsharp_call_start_unary;
            this.grpcsharp_call_start_client_streaming = DllImportsFromSharedLib_x86.grpcsharp_call_start_client_streaming;
            this.grpcsharp_call_start_server_streaming = DllImportsFromSharedLib_x86.grpcsharp_call_start_server_streaming;
            this.grpcsharp_call_start_duplex_streaming = DllImportsFromSharedLib_x86.grpcsharp_call_start_duplex_streaming;
            this.grpcsharp_call_send_message = DllImportsFromSharedLib_x86.grpcsharp_call_send_message;
            this.grpcsharp_call_send_close_from_client = DllImportsFromSharedLib_x86.grpcsharp_call_send_close_from_client;
            this.grpcsharp_call_send_status_from_server = DllImportsFromSharedLib_x86.grpcsharp_call_send_status_from_server;
            this.grpcsharp_call_recv_message = DllImportsFromSharedLib_x86.grpcsharp_call_recv_message;
            this.grpcsharp_call_recv_initial_metadata = DllImportsFromSharedLib_x86.grpcsharp_call_recv_initial_metadata;
            this.grpcsharp_call_start_serverside = DllImportsFromSharedLib_x86.grpcsharp_call_start_serverside;
            this.grpcsharp_call_send_initial_metadata = DllImportsFromSharedLib_x86.grpcsharp_call_send_initial_metadata;
            this.grpcsharp_call_set_credentials = DllImportsFromSharedLib_x86.grpcsharp_call_set_credentials;
            this.grpcsharp_call_get_peer = DllImportsFromSharedLib_x86.grpcsharp_call_get_peer;
            this.grpcsharp_call_destroy = DllImportsFromSharedLib_x86.grpcsharp_call_destroy;
            this.grpcsharp_channel_args_create = DllImportsFromSharedLib_x86.grpcsharp_channel_args_create;
            this.grpcsharp_channel_args_set_string = DllImportsFromSharedLib_x86.grpcsharp_channel_args_set_string;
            this.grpcsharp_channel_args_set_integer = DllImportsFromSharedLib_x86.grpcsharp_channel_args_set_integer;
            this.grpcsharp_channel_args_destroy = DllImportsFromSharedLib_x86.grpcsharp_channel_args_destroy;
            this.grpcsharp_override_default_ssl_roots = DllImportsFromSharedLib_x86.grpcsharp_override_default_ssl_roots;
            this.grpcsharp_ssl_credentials_create = DllImportsFromSharedLib_x86.grpcsharp_ssl_credentials_create;
            this.grpcsharp_composite_channel_credentials_create = DllImportsFromSharedLib_x86.grpcsharp_composite_channel_credentials_create;
            this.grpcsharp_channel_credentials_release = DllImportsFromSharedLib_x86.grpcsharp_channel_credentials_release;
            this.grpcsharp_insecure_channel_create = DllImportsFromSharedLib_x86.grpcsharp_insecure_channel_create;
            this.grpcsharp_secure_channel_create = DllImportsFromSharedLib_x86.grpcsharp_secure_channel_create;
            this.grpcsharp_channel_create_call = DllImportsFromSharedLib_x86.grpcsharp_channel_create_call;
            this.grpcsharp_channel_check_connectivity_state = DllImportsFromSharedLib_x86.grpcsharp_channel_check_connectivity_state;
            this.grpcsharp_channel_watch_connectivity_state = DllImportsFromSharedLib_x86.grpcsharp_channel_watch_connectivity_state;
            this.grpcsharp_channel_get_target = DllImportsFromSharedLib_x86.grpcsharp_channel_get_target;
            this.grpcsharp_channel_destroy = DllImportsFromSharedLib_x86.grpcsharp_channel_destroy;
            this.grpcsharp_sizeof_grpc_event = DllImportsFromSharedLib_x86.grpcsharp_sizeof_grpc_event;
            this.grpcsharp_completion_queue_create_async = DllImportsFromSharedLib_x86.grpcsharp_completion_queue_create_async;
            this.grpcsharp_completion_queue_create_sync = DllImportsFromSharedLib_x86.grpcsharp_completion_queue_create_sync;
            this.grpcsharp_completion_queue_shutdown = DllImportsFromSharedLib_x86.grpcsharp_completion_queue_shutdown;
            this.grpcsharp_completion_queue_next = DllImportsFromSharedLib_x86.grpcsharp_completion_queue_next;
            this.grpcsharp_completion_queue_pluck = DllImportsFromSharedLib_x86.grpcsharp_completion_queue_pluck;
            this.grpcsharp_completion_queue_destroy = DllImportsFromSharedLib_x86.grpcsharp_completion_queue_destroy;
            this.gprsharp_free = DllImportsFromSharedLib_x86.gprsharp_free;
            this.grpcsharp_metadata_array_create = DllImportsFromSharedLib_x86.grpcsharp_metadata_array_create;
            this.grpcsharp_metadata_array_add = DllImportsFromSharedLib_x86.grpcsharp_metadata_array_add;
            this.grpcsharp_metadata_array_count = DllImportsFromSharedLib_x86.grpcsharp_metadata_array_count;
            this.grpcsharp_metadata_array_get_key = DllImportsFromSharedLib_x86.grpcsharp_metadata_array_get_key;
            this.grpcsharp_metadata_array_get_value = DllImportsFromSharedLib_x86.grpcsharp_metadata_array_get_value;
            this.grpcsharp_metadata_array_destroy_full = DllImportsFromSharedLib_x86.grpcsharp_metadata_array_destroy_full;
            this.grpcsharp_redirect_log = DllImportsFromSharedLib_x86.grpcsharp_redirect_log;
            this.grpcsharp_native_callback_dispatcher_init = DllImportsFromSharedLib_x86.grpcsharp_native_callback_dispatcher_init;
            this.grpcsharp_metadata_credentials_create_from_plugin = DllImportsFromSharedLib_x86.grpcsharp_metadata_credentials_create_from_plugin;
            this.grpcsharp_metadata_credentials_notify_from_plugin = DllImportsFromSharedLib_x86.grpcsharp_metadata_credentials_notify_from_plugin;
            this.grpcsharp_ssl_server_credentials_create = DllImportsFromSharedLib_x86.grpcsharp_ssl_server_credentials_create;
            this.grpcsharp_server_credentials_release = DllImportsFromSharedLib_x86.grpcsharp_server_credentials_release;
            this.grpcsharp_server_create = DllImportsFromSharedLib_x86.grpcsharp_server_create;
            this.grpcsharp_server_register_completion_queue = DllImportsFromSharedLib_x86.grpcsharp_server_register_completion_queue;
            this.grpcsharp_server_add_insecure_http2_port = DllImportsFromSharedLib_x86.grpcsharp_server_add_insecure_http2_port;
            this.grpcsharp_server_add_secure_http2_port = DllImportsFromSharedLib_x86.grpcsharp_server_add_secure_http2_port;
            this.grpcsharp_server_start = DllImportsFromSharedLib_x86.grpcsharp_server_start;
            this.grpcsharp_server_request_call = DllImportsFromSharedLib_x86.grpcsharp_server_request_call;
            this.grpcsharp_server_cancel_all_calls = DllImportsFromSharedLib_x86.grpcsharp_server_cancel_all_calls;
            this.grpcsharp_server_shutdown_and_notify_callback = DllImportsFromSharedLib_x86.grpcsharp_server_shutdown_and_notify_callback;
            this.grpcsharp_server_destroy = DllImportsFromSharedLib_x86.grpcsharp_server_destroy;
            this.grpcsharp_call_auth_context = DllImportsFromSharedLib_x86.grpcsharp_call_auth_context;
            this.grpcsharp_auth_context_peer_identity_property_name = DllImportsFromSharedLib_x86.grpcsharp_auth_context_peer_identity_property_name;
            this.grpcsharp_auth_context_property_iterator = DllImportsFromSharedLib_x86.grpcsharp_auth_context_property_iterator;
            this.grpcsharp_auth_property_iterator_next = DllImportsFromSharedLib_x86.grpcsharp_auth_property_iterator_next;
            this.grpcsharp_auth_context_release = DllImportsFromSharedLib_x86.grpcsharp_auth_context_release;
            this.grpcsharp_slice_buffer_create = DllImportsFromSharedLib_x86.grpcsharp_slice_buffer_create;
            this.grpcsharp_slice_buffer_adjust_tail_space = DllImportsFromSharedLib_x86.grpcsharp_slice_buffer_adjust_tail_space;
            this.grpcsharp_slice_buffer_slice_count = DllImportsFromSharedLib_x86.grpcsharp_slice_buffer_slice_count;
            this.grpcsharp_slice_buffer_slice_peek = DllImportsFromSharedLib_x86.grpcsharp_slice_buffer_slice_peek;
            this.grpcsharp_slice_buffer_reset_and_unref = DllImportsFromSharedLib_x86.grpcsharp_slice_buffer_reset_and_unref;
            this.grpcsharp_slice_buffer_destroy = DllImportsFromSharedLib_x86.grpcsharp_slice_buffer_destroy;
            this.gprsharp_now = DllImportsFromSharedLib_x86.gprsharp_now;
            this.gprsharp_inf_future = DllImportsFromSharedLib_x86.gprsharp_inf_future;
            this.gprsharp_inf_past = DllImportsFromSharedLib_x86.gprsharp_inf_past;
            this.gprsharp_convert_clock_type = DllImportsFromSharedLib_x86.gprsharp_convert_clock_type;
            this.gprsharp_sizeof_timespec = DllImportsFromSharedLib_x86.gprsharp_sizeof_timespec;
            this.grpcsharp_test_callback = DllImportsFromSharedLib_x86.grpcsharp_test_callback;
            this.grpcsharp_test_nop = DllImportsFromSharedLib_x86.grpcsharp_test_nop;
            this.grpcsharp_test_override_method = DllImportsFromSharedLib_x86.grpcsharp_test_override_method;
            this.grpcsharp_test_call_start_unary_echo = DllImportsFromSharedLib_x86.grpcsharp_test_call_start_unary_echo;
        }

        public NativeMethods(DllImportsFromSharedLib_x64 unusedInstance)
        {
            this.grpcsharp_init = DllImportsFromSharedLib_x64.grpcsharp_init;
            this.grpcsharp_shutdown = DllImportsFromSharedLib_x64.grpcsharp_shutdown;
            this.grpcsharp_version_string = DllImportsFromSharedLib_x64.grpcsharp_version_string;
            this.grpcsharp_batch_context_create = DllImportsFromSharedLib_x64.grpcsharp_batch_context_create;
            this.grpcsharp_batch_context_recv_initial_metadata = DllImportsFromSharedLib_x64.grpcsharp_batch_context_recv_initial_metadata;
            this.grpcsharp_batch_context_recv_message_length = DllImportsFromSharedLib_x64.grpcsharp_batch_context_recv_message_length;
            this.grpcsharp_batch_context_recv_message_next_slice_peek = DllImportsFromSharedLib_x64.grpcsharp_batch_context_recv_message_next_slice_peek;
            this.grpcsharp_batch_context_recv_status_on_client_status = DllImportsFromSharedLib_x64.grpcsharp_batch_context_recv_status_on_client_status;
            this.grpcsharp_batch_context_recv_status_on_client_details = DllImportsFromSharedLib_x64.grpcsharp_batch_context_recv_status_on_client_details;
            this.grpcsharp_batch_context_recv_status_on_client_error_string = DllImportsFromSharedLib_x64.grpcsharp_batch_context_recv_status_on_client_error_string;
            this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = DllImportsFromSharedLib_x64.grpcsharp_batch_context_recv_status_on_client_trailing_metadata;
            this.grpcsharp_batch_context_recv_close_on_server_cancelled = DllImportsFromSharedLib_x64.grpcsharp_batch_context_recv_close_on_server_cancelled;
            this.grpcsharp_batch_context_reset = DllImportsFromSharedLib_x64.grpcsharp_batch_context_reset;
            this.grpcsharp_batch_context_destroy = DllImportsFromSharedLib_x64.grpcsharp_batch_context_destroy;
            this.grpcsharp_request_call_context_create = DllImportsFromSharedLib_x64.grpcsharp_request_call_context_create;
            this.grpcsharp_request_call_context_call = DllImportsFromSharedLib_x64.grpcsharp_request_call_context_call;
            this.grpcsharp_request_call_context_method = DllImportsFromSharedLib_x64.grpcsharp_request_call_context_method;
            this.grpcsharp_request_call_context_host = DllImportsFromSharedLib_x64.grpcsharp_request_call_context_host;
            this.grpcsharp_request_call_context_deadline = DllImportsFromSharedLib_x64.grpcsharp_request_call_context_deadline;
            this.grpcsharp_request_call_context_request_metadata = DllImportsFromSharedLib_x64.grpcsharp_request_call_context_request_metadata;
            this.grpcsharp_request_call_context_reset = DllImportsFromSharedLib_x64.grpcsharp_request_call_context_reset;
            this.grpcsharp_request_call_context_destroy = DllImportsFromSharedLib_x64.grpcsharp_request_call_context_destroy;
            this.grpcsharp_composite_call_credentials_create = DllImportsFromSharedLib_x64.grpcsharp_composite_call_credentials_create;
            this.grpcsharp_call_credentials_release = DllImportsFromSharedLib_x64.grpcsharp_call_credentials_release;
            this.grpcsharp_call_cancel = DllImportsFromSharedLib_x64.grpcsharp_call_cancel;
            this.grpcsharp_call_cancel_with_status = DllImportsFromSharedLib_x64.grpcsharp_call_cancel_with_status;
            this.grpcsharp_call_start_unary = DllImportsFromSharedLib_x64.grpcsharp_call_start_unary;
            this.grpcsharp_call_start_client_streaming = DllImportsFromSharedLib_x64.grpcsharp_call_start_client_streaming;
            this.grpcsharp_call_start_server_streaming = DllImportsFromSharedLib_x64.grpcsharp_call_start_server_streaming;
            this.grpcsharp_call_start_duplex_streaming = DllImportsFromSharedLib_x64.grpcsharp_call_start_duplex_streaming;
            this.grpcsharp_call_send_message = DllImportsFromSharedLib_x64.grpcsharp_call_send_message;
            this.grpcsharp_call_send_close_from_client = DllImportsFromSharedLib_x64.grpcsharp_call_send_close_from_client;
            this.grpcsharp_call_send_status_from_server = DllImportsFromSharedLib_x64.grpcsharp_call_send_status_from_server;
            this.grpcsharp_call_recv_message = DllImportsFromSharedLib_x64.grpcsharp_call_recv_message;
            this.grpcsharp_call_recv_initial_metadata = DllImportsFromSharedLib_x64.grpcsharp_call_recv_initial_metadata;
            this.grpcsharp_call_start_serverside = DllImportsFromSharedLib_x64.grpcsharp_call_start_serverside;
            this.grpcsharp_call_send_initial_metadata = DllImportsFromSharedLib_x64.grpcsharp_call_send_initial_metadata;
            this.grpcsharp_call_set_credentials = DllImportsFromSharedLib_x64.grpcsharp_call_set_credentials;
            this.grpcsharp_call_get_peer = DllImportsFromSharedLib_x64.grpcsharp_call_get_peer;
            this.grpcsharp_call_destroy = DllImportsFromSharedLib_x64.grpcsharp_call_destroy;
            this.grpcsharp_channel_args_create = DllImportsFromSharedLib_x64.grpcsharp_channel_args_create;
            this.grpcsharp_channel_args_set_string = DllImportsFromSharedLib_x64.grpcsharp_channel_args_set_string;
            this.grpcsharp_channel_args_set_integer = DllImportsFromSharedLib_x64.grpcsharp_channel_args_set_integer;
            this.grpcsharp_channel_args_destroy = DllImportsFromSharedLib_x64.grpcsharp_channel_args_destroy;
            this.grpcsharp_override_default_ssl_roots = DllImportsFromSharedLib_x64.grpcsharp_override_default_ssl_roots;
            this.grpcsharp_ssl_credentials_create = DllImportsFromSharedLib_x64.grpcsharp_ssl_credentials_create;
            this.grpcsharp_composite_channel_credentials_create = DllImportsFromSharedLib_x64.grpcsharp_composite_channel_credentials_create;
            this.grpcsharp_channel_credentials_release = DllImportsFromSharedLib_x64.grpcsharp_channel_credentials_release;
            this.grpcsharp_insecure_channel_create = DllImportsFromSharedLib_x64.grpcsharp_insecure_channel_create;
            this.grpcsharp_secure_channel_create = DllImportsFromSharedLib_x64.grpcsharp_secure_channel_create;
            this.grpcsharp_channel_create_call = DllImportsFromSharedLib_x64.grpcsharp_channel_create_call;
            this.grpcsharp_channel_check_connectivity_state = DllImportsFromSharedLib_x64.grpcsharp_channel_check_connectivity_state;
            this.grpcsharp_channel_watch_connectivity_state = DllImportsFromSharedLib_x64.grpcsharp_channel_watch_connectivity_state;
            this.grpcsharp_channel_get_target = DllImportsFromSharedLib_x64.grpcsharp_channel_get_target;
            this.grpcsharp_channel_destroy = DllImportsFromSharedLib_x64.grpcsharp_channel_destroy;
            this.grpcsharp_sizeof_grpc_event = DllImportsFromSharedLib_x64.grpcsharp_sizeof_grpc_event;
            this.grpcsharp_completion_queue_create_async = DllImportsFromSharedLib_x64.grpcsharp_completion_queue_create_async;
            this.grpcsharp_completion_queue_create_sync = DllImportsFromSharedLib_x64.grpcsharp_completion_queue_create_sync;
            this.grpcsharp_completion_queue_shutdown = DllImportsFromSharedLib_x64.grpcsharp_completion_queue_shutdown;
            this.grpcsharp_completion_queue_next = DllImportsFromSharedLib_x64.grpcsharp_completion_queue_next;
            this.grpcsharp_completion_queue_pluck = DllImportsFromSharedLib_x64.grpcsharp_completion_queue_pluck;
            this.grpcsharp_completion_queue_destroy = DllImportsFromSharedLib_x64.grpcsharp_completion_queue_destroy;
            this.gprsharp_free = DllImportsFromSharedLib_x64.gprsharp_free;
            this.grpcsharp_metadata_array_create = DllImportsFromSharedLib_x64.grpcsharp_metadata_array_create;
            this.grpcsharp_metadata_array_add = DllImportsFromSharedLib_x64.grpcsharp_metadata_array_add;
            this.grpcsharp_metadata_array_count = DllImportsFromSharedLib_x64.grpcsharp_metadata_array_count;
            this.grpcsharp_metadata_array_get_key = DllImportsFromSharedLib_x64.grpcsharp_metadata_array_get_key;
            this.grpcsharp_metadata_array_get_value = DllImportsFromSharedLib_x64.grpcsharp_metadata_array_get_value;
            this.grpcsharp_metadata_array_destroy_full = DllImportsFromSharedLib_x64.grpcsharp_metadata_array_destroy_full;
            this.grpcsharp_redirect_log = DllImportsFromSharedLib_x64.grpcsharp_redirect_log;
            this.grpcsharp_native_callback_dispatcher_init = DllImportsFromSharedLib_x64.grpcsharp_native_callback_dispatcher_init;
            this.grpcsharp_metadata_credentials_create_from_plugin = DllImportsFromSharedLib_x64.grpcsharp_metadata_credentials_create_from_plugin;
            this.grpcsharp_metadata_credentials_notify_from_plugin = DllImportsFromSharedLib_x64.grpcsharp_metadata_credentials_notify_from_plugin;
            this.grpcsharp_ssl_server_credentials_create = DllImportsFromSharedLib_x64.grpcsharp_ssl_server_credentials_create;
            this.grpcsharp_server_credentials_release = DllImportsFromSharedLib_x64.grpcsharp_server_credentials_release;
            this.grpcsharp_server_create = DllImportsFromSharedLib_x64.grpcsharp_server_create;
            this.grpcsharp_server_register_completion_queue = DllImportsFromSharedLib_x64.grpcsharp_server_register_completion_queue;
            this.grpcsharp_server_add_insecure_http2_port = DllImportsFromSharedLib_x64.grpcsharp_server_add_insecure_http2_port;
            this.grpcsharp_server_add_secure_http2_port = DllImportsFromSharedLib_x64.grpcsharp_server_add_secure_http2_port;
            this.grpcsharp_server_start = DllImportsFromSharedLib_x64.grpcsharp_server_start;
            this.grpcsharp_server_request_call = DllImportsFromSharedLib_x64.grpcsharp_server_request_call;
            this.grpcsharp_server_cancel_all_calls = DllImportsFromSharedLib_x64.grpcsharp_server_cancel_all_calls;
            this.grpcsharp_server_shutdown_and_notify_callback = DllImportsFromSharedLib_x64.grpcsharp_server_shutdown_and_notify_callback;
            this.grpcsharp_server_destroy = DllImportsFromSharedLib_x64.grpcsharp_server_destroy;
            this.grpcsharp_call_auth_context = DllImportsFromSharedLib_x64.grpcsharp_call_auth_context;
            this.grpcsharp_auth_context_peer_identity_property_name = DllImportsFromSharedLib_x64.grpcsharp_auth_context_peer_identity_property_name;
            this.grpcsharp_auth_context_property_iterator = DllImportsFromSharedLib_x64.grpcsharp_auth_context_property_iterator;
            this.grpcsharp_auth_property_iterator_next = DllImportsFromSharedLib_x64.grpcsharp_auth_property_iterator_next;
            this.grpcsharp_auth_context_release = DllImportsFromSharedLib_x64.grpcsharp_auth_context_release;
            this.grpcsharp_slice_buffer_create = DllImportsFromSharedLib_x64.grpcsharp_slice_buffer_create;
            this.grpcsharp_slice_buffer_adjust_tail_space = DllImportsFromSharedLib_x64.grpcsharp_slice_buffer_adjust_tail_space;
            this.grpcsharp_slice_buffer_slice_count = DllImportsFromSharedLib_x64.grpcsharp_slice_buffer_slice_count;
            this.grpcsharp_slice_buffer_slice_peek = DllImportsFromSharedLib_x64.grpcsharp_slice_buffer_slice_peek;
            this.grpcsharp_slice_buffer_reset_and_unref = DllImportsFromSharedLib_x64.grpcsharp_slice_buffer_reset_and_unref;
            this.grpcsharp_slice_buffer_destroy = DllImportsFromSharedLib_x64.grpcsharp_slice_buffer_destroy;
            this.gprsharp_now = DllImportsFromSharedLib_x64.gprsharp_now;
            this.gprsharp_inf_future = DllImportsFromSharedLib_x64.gprsharp_inf_future;
            this.gprsharp_inf_past = DllImportsFromSharedLib_x64.gprsharp_inf_past;
            this.gprsharp_convert_clock_type = DllImportsFromSharedLib_x64.gprsharp_convert_clock_type;
            this.gprsharp_sizeof_timespec = DllImportsFromSharedLib_x64.gprsharp_sizeof_timespec;
            this.grpcsharp_test_callback = DllImportsFromSharedLib_x64.grpcsharp_test_callback;
            this.grpcsharp_test_nop = DllImportsFromSharedLib_x64.grpcsharp_test_nop;
            this.grpcsharp_test_override_method = DllImportsFromSharedLib_x64.grpcsharp_test_override_method;
            this.grpcsharp_test_call_start_unary_echo = DllImportsFromSharedLib_x64.grpcsharp_test_call_start_unary_echo;
        }

        public NativeMethods(DllImportsFromSharedLib_arm64 unusedInstance)
        {
            this.grpcsharp_init = DllImportsFromSharedLib_arm64.grpcsharp_init;
            this.grpcsharp_shutdown = DllImportsFromSharedLib_arm64.grpcsharp_shutdown;
            this.grpcsharp_version_string = DllImportsFromSharedLib_arm64.grpcsharp_version_string;
            this.grpcsharp_batch_context_create = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_create;
            this.grpcsharp_batch_context_recv_initial_metadata = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_recv_initial_metadata;
            this.grpcsharp_batch_context_recv_message_length = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_recv_message_length;
            this.grpcsharp_batch_context_recv_message_next_slice_peek = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_recv_message_next_slice_peek;
            this.grpcsharp_batch_context_recv_status_on_client_status = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_recv_status_on_client_status;
            this.grpcsharp_batch_context_recv_status_on_client_details = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_recv_status_on_client_details;
            this.grpcsharp_batch_context_recv_status_on_client_error_string = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_recv_status_on_client_error_string;
            this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_recv_status_on_client_trailing_metadata;
            this.grpcsharp_batch_context_recv_close_on_server_cancelled = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_recv_close_on_server_cancelled;
            this.grpcsharp_batch_context_reset = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_reset;
            this.grpcsharp_batch_context_destroy = DllImportsFromSharedLib_arm64.grpcsharp_batch_context_destroy;
            this.grpcsharp_request_call_context_create = DllImportsFromSharedLib_arm64.grpcsharp_request_call_context_create;
            this.grpcsharp_request_call_context_call = DllImportsFromSharedLib_arm64.grpcsharp_request_call_context_call;
            this.grpcsharp_request_call_context_method = DllImportsFromSharedLib_arm64.grpcsharp_request_call_context_method;
            this.grpcsharp_request_call_context_host = DllImportsFromSharedLib_arm64.grpcsharp_request_call_context_host;
            this.grpcsharp_request_call_context_deadline = DllImportsFromSharedLib_arm64.grpcsharp_request_call_context_deadline;
            this.grpcsharp_request_call_context_request_metadata = DllImportsFromSharedLib_arm64.grpcsharp_request_call_context_request_metadata;
            this.grpcsharp_request_call_context_reset = DllImportsFromSharedLib_arm64.grpcsharp_request_call_context_reset;
            this.grpcsharp_request_call_context_destroy = DllImportsFromSharedLib_arm64.grpcsharp_request_call_context_destroy;
            this.grpcsharp_composite_call_credentials_create = DllImportsFromSharedLib_arm64.grpcsharp_composite_call_credentials_create;
            this.grpcsharp_call_credentials_release = DllImportsFromSharedLib_arm64.grpcsharp_call_credentials_release;
            this.grpcsharp_call_cancel = DllImportsFromSharedLib_arm64.grpcsharp_call_cancel;
            this.grpcsharp_call_cancel_with_status = DllImportsFromSharedLib_arm64.grpcsharp_call_cancel_with_status;
            this.grpcsharp_call_start_unary = DllImportsFromSharedLib_arm64.grpcsharp_call_start_unary;
            this.grpcsharp_call_start_client_streaming = DllImportsFromSharedLib_arm64.grpcsharp_call_start_client_streaming;
            this.grpcsharp_call_start_server_streaming = DllImportsFromSharedLib_arm64.grpcsharp_call_start_server_streaming;
            this.grpcsharp_call_start_duplex_streaming = DllImportsFromSharedLib_arm64.grpcsharp_call_start_duplex_streaming;
            this.grpcsharp_call_send_message = DllImportsFromSharedLib_arm64.grpcsharp_call_send_message;
            this.grpcsharp_call_send_close_from_client = DllImportsFromSharedLib_arm64.grpcsharp_call_send_close_from_client;
            this.grpcsharp_call_send_status_from_server = DllImportsFromSharedLib_arm64.grpcsharp_call_send_status_from_server;
            this.grpcsharp_call_recv_message = DllImportsFromSharedLib_arm64.grpcsharp_call_recv_message;
            this.grpcsharp_call_recv_initial_metadata = DllImportsFromSharedLib_arm64.grpcsharp_call_recv_initial_metadata;
            this.grpcsharp_call_start_serverside = DllImportsFromSharedLib_arm64.grpcsharp_call_start_serverside;
            this.grpcsharp_call_send_initial_metadata = DllImportsFromSharedLib_arm64.grpcsharp_call_send_initial_metadata;
            this.grpcsharp_call_set_credentials = DllImportsFromSharedLib_arm64.grpcsharp_call_set_credentials;
            this.grpcsharp_call_get_peer = DllImportsFromSharedLib_arm64.grpcsharp_call_get_peer;
            this.grpcsharp_call_destroy = DllImportsFromSharedLib_arm64.grpcsharp_call_destroy;
            this.grpcsharp_channel_args_create = DllImportsFromSharedLib_arm64.grpcsharp_channel_args_create;
            this.grpcsharp_channel_args_set_string = DllImportsFromSharedLib_arm64.grpcsharp_channel_args_set_string;
            this.grpcsharp_channel_args_set_integer = DllImportsFromSharedLib_arm64.grpcsharp_channel_args_set_integer;
            this.grpcsharp_channel_args_destroy = DllImportsFromSharedLib_arm64.grpcsharp_channel_args_destroy;
            this.grpcsharp_override_default_ssl_roots = DllImportsFromSharedLib_arm64.grpcsharp_override_default_ssl_roots;
            this.grpcsharp_ssl_credentials_create = DllImportsFromSharedLib_arm64.grpcsharp_ssl_credentials_create;
            this.grpcsharp_composite_channel_credentials_create = DllImportsFromSharedLib_arm64.grpcsharp_composite_channel_credentials_create;
            this.grpcsharp_channel_credentials_release = DllImportsFromSharedLib_arm64.grpcsharp_channel_credentials_release;
            this.grpcsharp_insecure_channel_create = DllImportsFromSharedLib_arm64.grpcsharp_insecure_channel_create;
            this.grpcsharp_secure_channel_create = DllImportsFromSharedLib_arm64.grpcsharp_secure_channel_create;
            this.grpcsharp_channel_create_call = DllImportsFromSharedLib_arm64.grpcsharp_channel_create_call;
            this.grpcsharp_channel_check_connectivity_state = DllImportsFromSharedLib_arm64.grpcsharp_channel_check_connectivity_state;
            this.grpcsharp_channel_watch_connectivity_state = DllImportsFromSharedLib_arm64.grpcsharp_channel_watch_connectivity_state;
            this.grpcsharp_channel_get_target = DllImportsFromSharedLib_arm64.grpcsharp_channel_get_target;
            this.grpcsharp_channel_destroy = DllImportsFromSharedLib_arm64.grpcsharp_channel_destroy;
            this.grpcsharp_sizeof_grpc_event = DllImportsFromSharedLib_arm64.grpcsharp_sizeof_grpc_event;
            this.grpcsharp_completion_queue_create_async = DllImportsFromSharedLib_arm64.grpcsharp_completion_queue_create_async;
            this.grpcsharp_completion_queue_create_sync = DllImportsFromSharedLib_arm64.grpcsharp_completion_queue_create_sync;
            this.grpcsharp_completion_queue_shutdown = DllImportsFromSharedLib_arm64.grpcsharp_completion_queue_shutdown;
            this.grpcsharp_completion_queue_next = DllImportsFromSharedLib_arm64.grpcsharp_completion_queue_next;
            this.grpcsharp_completion_queue_pluck = DllImportsFromSharedLib_arm64.grpcsharp_completion_queue_pluck;
            this.grpcsharp_completion_queue_destroy = DllImportsFromSharedLib_arm64.grpcsharp_completion_queue_destroy;
            this.gprsharp_free = DllImportsFromSharedLib_arm64.gprsharp_free;
            this.grpcsharp_metadata_array_create = DllImportsFromSharedLib_arm64.grpcsharp_metadata_array_create;
            this.grpcsharp_metadata_array_add = DllImportsFromSharedLib_arm64.grpcsharp_metadata_array_add;
            this.grpcsharp_metadata_array_count = DllImportsFromSharedLib_arm64.grpcsharp_metadata_array_count;
            this.grpcsharp_metadata_array_get_key = DllImportsFromSharedLib_arm64.grpcsharp_metadata_array_get_key;
            this.grpcsharp_metadata_array_get_value = DllImportsFromSharedLib_arm64.grpcsharp_metadata_array_get_value;
            this.grpcsharp_metadata_array_destroy_full = DllImportsFromSharedLib_arm64.grpcsharp_metadata_array_destroy_full;
            this.grpcsharp_redirect_log = DllImportsFromSharedLib_arm64.grpcsharp_redirect_log;
            this.grpcsharp_native_callback_dispatcher_init = DllImportsFromSharedLib_arm64.grpcsharp_native_callback_dispatcher_init;
            this.grpcsharp_metadata_credentials_create_from_plugin = DllImportsFromSharedLib_arm64.grpcsharp_metadata_credentials_create_from_plugin;
            this.grpcsharp_metadata_credentials_notify_from_plugin = DllImportsFromSharedLib_arm64.grpcsharp_metadata_credentials_notify_from_plugin;
            this.grpcsharp_ssl_server_credentials_create = DllImportsFromSharedLib_arm64.grpcsharp_ssl_server_credentials_create;
            this.grpcsharp_server_credentials_release = DllImportsFromSharedLib_arm64.grpcsharp_server_credentials_release;
            this.grpcsharp_server_create = DllImportsFromSharedLib_arm64.grpcsharp_server_create;
            this.grpcsharp_server_register_completion_queue = DllImportsFromSharedLib_arm64.grpcsharp_server_register_completion_queue;
            this.grpcsharp_server_add_insecure_http2_port = DllImportsFromSharedLib_arm64.grpcsharp_server_add_insecure_http2_port;
            this.grpcsharp_server_add_secure_http2_port = DllImportsFromSharedLib_arm64.grpcsharp_server_add_secure_http2_port;
            this.grpcsharp_server_start = DllImportsFromSharedLib_arm64.grpcsharp_server_start;
            this.grpcsharp_server_request_call = DllImportsFromSharedLib_arm64.grpcsharp_server_request_call;
            this.grpcsharp_server_cancel_all_calls = DllImportsFromSharedLib_arm64.grpcsharp_server_cancel_all_calls;
            this.grpcsharp_server_shutdown_and_notify_callback = DllImportsFromSharedLib_arm64.grpcsharp_server_shutdown_and_notify_callback;
            this.grpcsharp_server_destroy = DllImportsFromSharedLib_arm64.grpcsharp_server_destroy;
            this.grpcsharp_call_auth_context = DllImportsFromSharedLib_arm64.grpcsharp_call_auth_context;
            this.grpcsharp_auth_context_peer_identity_property_name = DllImportsFromSharedLib_arm64.grpcsharp_auth_context_peer_identity_property_name;
            this.grpcsharp_auth_context_property_iterator = DllImportsFromSharedLib_arm64.grpcsharp_auth_context_property_iterator;
            this.grpcsharp_auth_property_iterator_next = DllImportsFromSharedLib_arm64.grpcsharp_auth_property_iterator_next;
            this.grpcsharp_auth_context_release = DllImportsFromSharedLib_arm64.grpcsharp_auth_context_release;
            this.grpcsharp_slice_buffer_create = DllImportsFromSharedLib_arm64.grpcsharp_slice_buffer_create;
            this.grpcsharp_slice_buffer_adjust_tail_space = DllImportsFromSharedLib_arm64.grpcsharp_slice_buffer_adjust_tail_space;
            this.grpcsharp_slice_buffer_slice_count = DllImportsFromSharedLib_arm64.grpcsharp_slice_buffer_slice_count;
            this.grpcsharp_slice_buffer_slice_peek = DllImportsFromSharedLib_arm64.grpcsharp_slice_buffer_slice_peek;
            this.grpcsharp_slice_buffer_reset_and_unref = DllImportsFromSharedLib_arm64.grpcsharp_slice_buffer_reset_and_unref;
            this.grpcsharp_slice_buffer_destroy = DllImportsFromSharedLib_arm64.grpcsharp_slice_buffer_destroy;
            this.gprsharp_now = DllImportsFromSharedLib_arm64.gprsharp_now;
            this.gprsharp_inf_future = DllImportsFromSharedLib_arm64.gprsharp_inf_future;
            this.gprsharp_inf_past = DllImportsFromSharedLib_arm64.gprsharp_inf_past;
            this.gprsharp_convert_clock_type = DllImportsFromSharedLib_arm64.gprsharp_convert_clock_type;
            this.gprsharp_sizeof_timespec = DllImportsFromSharedLib_arm64.gprsharp_sizeof_timespec;
            this.grpcsharp_test_callback = DllImportsFromSharedLib_arm64.grpcsharp_test_callback;
            this.grpcsharp_test_nop = DllImportsFromSharedLib_arm64.grpcsharp_test_nop;
            this.grpcsharp_test_override_method = DllImportsFromSharedLib_arm64.grpcsharp_test_override_method;
            this.grpcsharp_test_call_start_unary_echo = DllImportsFromSharedLib_arm64.grpcsharp_test_call_start_unary_echo;
        }

        public NativeMethods(DllImportsFromSharedLib_x86_dll unusedInstance)
        {
            this.grpcsharp_init = DllImportsFromSharedLib_x86_dll.grpcsharp_init;
            this.grpcsharp_shutdown = DllImportsFromSharedLib_x86_dll.grpcsharp_shutdown;
            this.grpcsharp_version_string = DllImportsFromSharedLib_x86_dll.grpcsharp_version_string;
            this.grpcsharp_batch_context_create = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_create;
            this.grpcsharp_batch_context_recv_initial_metadata = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_recv_initial_metadata;
            this.grpcsharp_batch_context_recv_message_length = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_recv_message_length;
            this.grpcsharp_batch_context_recv_message_next_slice_peek = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_recv_message_next_slice_peek;
            this.grpcsharp_batch_context_recv_status_on_client_status = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_recv_status_on_client_status;
            this.grpcsharp_batch_context_recv_status_on_client_details = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_recv_status_on_client_details;
            this.grpcsharp_batch_context_recv_status_on_client_error_string = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_recv_status_on_client_error_string;
            this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_recv_status_on_client_trailing_metadata;
            this.grpcsharp_batch_context_recv_close_on_server_cancelled = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_recv_close_on_server_cancelled;
            this.grpcsharp_batch_context_reset = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_reset;
            this.grpcsharp_batch_context_destroy = DllImportsFromSharedLib_x86_dll.grpcsharp_batch_context_destroy;
            this.grpcsharp_request_call_context_create = DllImportsFromSharedLib_x86_dll.grpcsharp_request_call_context_create;
            this.grpcsharp_request_call_context_call = DllImportsFromSharedLib_x86_dll.grpcsharp_request_call_context_call;
            this.grpcsharp_request_call_context_method = DllImportsFromSharedLib_x86_dll.grpcsharp_request_call_context_method;
            this.grpcsharp_request_call_context_host = DllImportsFromSharedLib_x86_dll.grpcsharp_request_call_context_host;
            this.grpcsharp_request_call_context_deadline = DllImportsFromSharedLib_x86_dll.grpcsharp_request_call_context_deadline;
            this.grpcsharp_request_call_context_request_metadata = DllImportsFromSharedLib_x86_dll.grpcsharp_request_call_context_request_metadata;
            this.grpcsharp_request_call_context_reset = DllImportsFromSharedLib_x86_dll.grpcsharp_request_call_context_reset;
            this.grpcsharp_request_call_context_destroy = DllImportsFromSharedLib_x86_dll.grpcsharp_request_call_context_destroy;
            this.grpcsharp_composite_call_credentials_create = DllImportsFromSharedLib_x86_dll.grpcsharp_composite_call_credentials_create;
            this.grpcsharp_call_credentials_release = DllImportsFromSharedLib_x86_dll.grpcsharp_call_credentials_release;
            this.grpcsharp_call_cancel = DllImportsFromSharedLib_x86_dll.grpcsharp_call_cancel;
            this.grpcsharp_call_cancel_with_status = DllImportsFromSharedLib_x86_dll.grpcsharp_call_cancel_with_status;
            this.grpcsharp_call_start_unary = DllImportsFromSharedLib_x86_dll.grpcsharp_call_start_unary;
            this.grpcsharp_call_start_client_streaming = DllImportsFromSharedLib_x86_dll.grpcsharp_call_start_client_streaming;
            this.grpcsharp_call_start_server_streaming = DllImportsFromSharedLib_x86_dll.grpcsharp_call_start_server_streaming;
            this.grpcsharp_call_start_duplex_streaming = DllImportsFromSharedLib_x86_dll.grpcsharp_call_start_duplex_streaming;
            this.grpcsharp_call_send_message = DllImportsFromSharedLib_x86_dll.grpcsharp_call_send_message;
            this.grpcsharp_call_send_close_from_client = DllImportsFromSharedLib_x86_dll.grpcsharp_call_send_close_from_client;
            this.grpcsharp_call_send_status_from_server = DllImportsFromSharedLib_x86_dll.grpcsharp_call_send_status_from_server;
            this.grpcsharp_call_recv_message = DllImportsFromSharedLib_x86_dll.grpcsharp_call_recv_message;
            this.grpcsharp_call_recv_initial_metadata = DllImportsFromSharedLib_x86_dll.grpcsharp_call_recv_initial_metadata;
            this.grpcsharp_call_start_serverside = DllImportsFromSharedLib_x86_dll.grpcsharp_call_start_serverside;
            this.grpcsharp_call_send_initial_metadata = DllImportsFromSharedLib_x86_dll.grpcsharp_call_send_initial_metadata;
            this.grpcsharp_call_set_credentials = DllImportsFromSharedLib_x86_dll.grpcsharp_call_set_credentials;
            this.grpcsharp_call_get_peer = DllImportsFromSharedLib_x86_dll.grpcsharp_call_get_peer;
            this.grpcsharp_call_destroy = DllImportsFromSharedLib_x86_dll.grpcsharp_call_destroy;
            this.grpcsharp_channel_args_create = DllImportsFromSharedLib_x86_dll.grpcsharp_channel_args_create;
            this.grpcsharp_channel_args_set_string = DllImportsFromSharedLib_x86_dll.grpcsharp_channel_args_set_string;
            this.grpcsharp_channel_args_set_integer = DllImportsFromSharedLib_x86_dll.grpcsharp_channel_args_set_integer;
            this.grpcsharp_channel_args_destroy = DllImportsFromSharedLib_x86_dll.grpcsharp_channel_args_destroy;
            this.grpcsharp_override_default_ssl_roots = DllImportsFromSharedLib_x86_dll.grpcsharp_override_default_ssl_roots;
            this.grpcsharp_ssl_credentials_create = DllImportsFromSharedLib_x86_dll.grpcsharp_ssl_credentials_create;
            this.grpcsharp_composite_channel_credentials_create = DllImportsFromSharedLib_x86_dll.grpcsharp_composite_channel_credentials_create;
            this.grpcsharp_channel_credentials_release = DllImportsFromSharedLib_x86_dll.grpcsharp_channel_credentials_release;
            this.grpcsharp_insecure_channel_create = DllImportsFromSharedLib_x86_dll.grpcsharp_insecure_channel_create;
            this.grpcsharp_secure_channel_create = DllImportsFromSharedLib_x86_dll.grpcsharp_secure_channel_create;
            this.grpcsharp_channel_create_call = DllImportsFromSharedLib_x86_dll.grpcsharp_channel_create_call;
            this.grpcsharp_channel_check_connectivity_state = DllImportsFromSharedLib_x86_dll.grpcsharp_channel_check_connectivity_state;
            this.grpcsharp_channel_watch_connectivity_state = DllImportsFromSharedLib_x86_dll.grpcsharp_channel_watch_connectivity_state;
            this.grpcsharp_channel_get_target = DllImportsFromSharedLib_x86_dll.grpcsharp_channel_get_target;
            this.grpcsharp_channel_destroy = DllImportsFromSharedLib_x86_dll.grpcsharp_channel_destroy;
            this.grpcsharp_sizeof_grpc_event = DllImportsFromSharedLib_x86_dll.grpcsharp_sizeof_grpc_event;
            this.grpcsharp_completion_queue_create_async = DllImportsFromSharedLib_x86_dll.grpcsharp_completion_queue_create_async;
            this.grpcsharp_completion_queue_create_sync = DllImportsFromSharedLib_x86_dll.grpcsharp_completion_queue_create_sync;
            this.grpcsharp_completion_queue_shutdown = DllImportsFromSharedLib_x86_dll.grpcsharp_completion_queue_shutdown;
            this.grpcsharp_completion_queue_next = DllImportsFromSharedLib_x86_dll.grpcsharp_completion_queue_next;
            this.grpcsharp_completion_queue_pluck = DllImportsFromSharedLib_x86_dll.grpcsharp_completion_queue_pluck;
            this.grpcsharp_completion_queue_destroy = DllImportsFromSharedLib_x86_dll.grpcsharp_completion_queue_destroy;
            this.gprsharp_free = DllImportsFromSharedLib_x86_dll.gprsharp_free;
            this.grpcsharp_metadata_array_create = DllImportsFromSharedLib_x86_dll.grpcsharp_metadata_array_create;
            this.grpcsharp_metadata_array_add = DllImportsFromSharedLib_x86_dll.grpcsharp_metadata_array_add;
            this.grpcsharp_metadata_array_count = DllImportsFromSharedLib_x86_dll.grpcsharp_metadata_array_count;
            this.grpcsharp_metadata_array_get_key = DllImportsFromSharedLib_x86_dll.grpcsharp_metadata_array_get_key;
            this.grpcsharp_metadata_array_get_value = DllImportsFromSharedLib_x86_dll.grpcsharp_metadata_array_get_value;
            this.grpcsharp_metadata_array_destroy_full = DllImportsFromSharedLib_x86_dll.grpcsharp_metadata_array_destroy_full;
            this.grpcsharp_redirect_log = DllImportsFromSharedLib_x86_dll.grpcsharp_redirect_log;
            this.grpcsharp_native_callback_dispatcher_init = DllImportsFromSharedLib_x86_dll.grpcsharp_native_callback_dispatcher_init;
            this.grpcsharp_metadata_credentials_create_from_plugin = DllImportsFromSharedLib_x86_dll.grpcsharp_metadata_credentials_create_from_plugin;
            this.grpcsharp_metadata_credentials_notify_from_plugin = DllImportsFromSharedLib_x86_dll.grpcsharp_metadata_credentials_notify_from_plugin;
            this.grpcsharp_ssl_server_credentials_create = DllImportsFromSharedLib_x86_dll.grpcsharp_ssl_server_credentials_create;
            this.grpcsharp_server_credentials_release = DllImportsFromSharedLib_x86_dll.grpcsharp_server_credentials_release;
            this.grpcsharp_server_create = DllImportsFromSharedLib_x86_dll.grpcsharp_server_create;
            this.grpcsharp_server_register_completion_queue = DllImportsFromSharedLib_x86_dll.grpcsharp_server_register_completion_queue;
            this.grpcsharp_server_add_insecure_http2_port = DllImportsFromSharedLib_x86_dll.grpcsharp_server_add_insecure_http2_port;
            this.grpcsharp_server_add_secure_http2_port = DllImportsFromSharedLib_x86_dll.grpcsharp_server_add_secure_http2_port;
            this.grpcsharp_server_start = DllImportsFromSharedLib_x86_dll.grpcsharp_server_start;
            this.grpcsharp_server_request_call = DllImportsFromSharedLib_x86_dll.grpcsharp_server_request_call;
            this.grpcsharp_server_cancel_all_calls = DllImportsFromSharedLib_x86_dll.grpcsharp_server_cancel_all_calls;
            this.grpcsharp_server_shutdown_and_notify_callback = DllImportsFromSharedLib_x86_dll.grpcsharp_server_shutdown_and_notify_callback;
            this.grpcsharp_server_destroy = DllImportsFromSharedLib_x86_dll.grpcsharp_server_destroy;
            this.grpcsharp_call_auth_context = DllImportsFromSharedLib_x86_dll.grpcsharp_call_auth_context;
            this.grpcsharp_auth_context_peer_identity_property_name = DllImportsFromSharedLib_x86_dll.grpcsharp_auth_context_peer_identity_property_name;
            this.grpcsharp_auth_context_property_iterator = DllImportsFromSharedLib_x86_dll.grpcsharp_auth_context_property_iterator;
            this.grpcsharp_auth_property_iterator_next = DllImportsFromSharedLib_x86_dll.grpcsharp_auth_property_iterator_next;
            this.grpcsharp_auth_context_release = DllImportsFromSharedLib_x86_dll.grpcsharp_auth_context_release;
            this.grpcsharp_slice_buffer_create = DllImportsFromSharedLib_x86_dll.grpcsharp_slice_buffer_create;
            this.grpcsharp_slice_buffer_adjust_tail_space = DllImportsFromSharedLib_x86_dll.grpcsharp_slice_buffer_adjust_tail_space;
            this.grpcsharp_slice_buffer_slice_count = DllImportsFromSharedLib_x86_dll.grpcsharp_slice_buffer_slice_count;
            this.grpcsharp_slice_buffer_slice_peek = DllImportsFromSharedLib_x86_dll.grpcsharp_slice_buffer_slice_peek;
            this.grpcsharp_slice_buffer_reset_and_unref = DllImportsFromSharedLib_x86_dll.grpcsharp_slice_buffer_reset_and_unref;
            this.grpcsharp_slice_buffer_destroy = DllImportsFromSharedLib_x86_dll.grpcsharp_slice_buffer_destroy;
            this.gprsharp_now = DllImportsFromSharedLib_x86_dll.gprsharp_now;
            this.gprsharp_inf_future = DllImportsFromSharedLib_x86_dll.gprsharp_inf_future;
            this.gprsharp_inf_past = DllImportsFromSharedLib_x86_dll.gprsharp_inf_past;
            this.gprsharp_convert_clock_type = DllImportsFromSharedLib_x86_dll.gprsharp_convert_clock_type;
            this.gprsharp_sizeof_timespec = DllImportsFromSharedLib_x86_dll.gprsharp_sizeof_timespec;
            this.grpcsharp_test_callback = DllImportsFromSharedLib_x86_dll.grpcsharp_test_callback;
            this.grpcsharp_test_nop = DllImportsFromSharedLib_x86_dll.grpcsharp_test_nop;
            this.grpcsharp_test_override_method = DllImportsFromSharedLib_x86_dll.grpcsharp_test_override_method;
            this.grpcsharp_test_call_start_unary_echo = DllImportsFromSharedLib_x86_dll.grpcsharp_test_call_start_unary_echo;
        }

        public NativeMethods(DllImportsFromSharedLib_x64_dll unusedInstance)
        {
            this.grpcsharp_init = DllImportsFromSharedLib_x64_dll.grpcsharp_init;
            this.grpcsharp_shutdown = DllImportsFromSharedLib_x64_dll.grpcsharp_shutdown;
            this.grpcsharp_version_string = DllImportsFromSharedLib_x64_dll.grpcsharp_version_string;
            this.grpcsharp_batch_context_create = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_create;
            this.grpcsharp_batch_context_recv_initial_metadata = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_recv_initial_metadata;
            this.grpcsharp_batch_context_recv_message_length = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_recv_message_length;
            this.grpcsharp_batch_context_recv_message_next_slice_peek = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_recv_message_next_slice_peek;
            this.grpcsharp_batch_context_recv_status_on_client_status = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_recv_status_on_client_status;
            this.grpcsharp_batch_context_recv_status_on_client_details = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_recv_status_on_client_details;
            this.grpcsharp_batch_context_recv_status_on_client_error_string = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_recv_status_on_client_error_string;
            this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_recv_status_on_client_trailing_metadata;
            this.grpcsharp_batch_context_recv_close_on_server_cancelled = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_recv_close_on_server_cancelled;
            this.grpcsharp_batch_context_reset = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_reset;
            this.grpcsharp_batch_context_destroy = DllImportsFromSharedLib_x64_dll.grpcsharp_batch_context_destroy;
            this.grpcsharp_request_call_context_create = DllImportsFromSharedLib_x64_dll.grpcsharp_request_call_context_create;
            this.grpcsharp_request_call_context_call = DllImportsFromSharedLib_x64_dll.grpcsharp_request_call_context_call;
            this.grpcsharp_request_call_context_method = DllImportsFromSharedLib_x64_dll.grpcsharp_request_call_context_method;
            this.grpcsharp_request_call_context_host = DllImportsFromSharedLib_x64_dll.grpcsharp_request_call_context_host;
            this.grpcsharp_request_call_context_deadline = DllImportsFromSharedLib_x64_dll.grpcsharp_request_call_context_deadline;
            this.grpcsharp_request_call_context_request_metadata = DllImportsFromSharedLib_x64_dll.grpcsharp_request_call_context_request_metadata;
            this.grpcsharp_request_call_context_reset = DllImportsFromSharedLib_x64_dll.grpcsharp_request_call_context_reset;
            this.grpcsharp_request_call_context_destroy = DllImportsFromSharedLib_x64_dll.grpcsharp_request_call_context_destroy;
            this.grpcsharp_composite_call_credentials_create = DllImportsFromSharedLib_x64_dll.grpcsharp_composite_call_credentials_create;
            this.grpcsharp_call_credentials_release = DllImportsFromSharedLib_x64_dll.grpcsharp_call_credentials_release;
            this.grpcsharp_call_cancel = DllImportsFromSharedLib_x64_dll.grpcsharp_call_cancel;
            this.grpcsharp_call_cancel_with_status = DllImportsFromSharedLib_x64_dll.grpcsharp_call_cancel_with_status;
            this.grpcsharp_call_start_unary = DllImportsFromSharedLib_x64_dll.grpcsharp_call_start_unary;
            this.grpcsharp_call_start_client_streaming = DllImportsFromSharedLib_x64_dll.grpcsharp_call_start_client_streaming;
            this.grpcsharp_call_start_server_streaming = DllImportsFromSharedLib_x64_dll.grpcsharp_call_start_server_streaming;
            this.grpcsharp_call_start_duplex_streaming = DllImportsFromSharedLib_x64_dll.grpcsharp_call_start_duplex_streaming;
            this.grpcsharp_call_send_message = DllImportsFromSharedLib_x64_dll.grpcsharp_call_send_message;
            this.grpcsharp_call_send_close_from_client = DllImportsFromSharedLib_x64_dll.grpcsharp_call_send_close_from_client;
            this.grpcsharp_call_send_status_from_server = DllImportsFromSharedLib_x64_dll.grpcsharp_call_send_status_from_server;
            this.grpcsharp_call_recv_message = DllImportsFromSharedLib_x64_dll.grpcsharp_call_recv_message;
            this.grpcsharp_call_recv_initial_metadata = DllImportsFromSharedLib_x64_dll.grpcsharp_call_recv_initial_metadata;
            this.grpcsharp_call_start_serverside = DllImportsFromSharedLib_x64_dll.grpcsharp_call_start_serverside;
            this.grpcsharp_call_send_initial_metadata = DllImportsFromSharedLib_x64_dll.grpcsharp_call_send_initial_metadata;
            this.grpcsharp_call_set_credentials = DllImportsFromSharedLib_x64_dll.grpcsharp_call_set_credentials;
            this.grpcsharp_call_get_peer = DllImportsFromSharedLib_x64_dll.grpcsharp_call_get_peer;
            this.grpcsharp_call_destroy = DllImportsFromSharedLib_x64_dll.grpcsharp_call_destroy;
            this.grpcsharp_channel_args_create = DllImportsFromSharedLib_x64_dll.grpcsharp_channel_args_create;
            this.grpcsharp_channel_args_set_string = DllImportsFromSharedLib_x64_dll.grpcsharp_channel_args_set_string;
            this.grpcsharp_channel_args_set_integer = DllImportsFromSharedLib_x64_dll.grpcsharp_channel_args_set_integer;
            this.grpcsharp_channel_args_destroy = DllImportsFromSharedLib_x64_dll.grpcsharp_channel_args_destroy;
            this.grpcsharp_override_default_ssl_roots = DllImportsFromSharedLib_x64_dll.grpcsharp_override_default_ssl_roots;
            this.grpcsharp_ssl_credentials_create = DllImportsFromSharedLib_x64_dll.grpcsharp_ssl_credentials_create;
            this.grpcsharp_composite_channel_credentials_create = DllImportsFromSharedLib_x64_dll.grpcsharp_composite_channel_credentials_create;
            this.grpcsharp_channel_credentials_release = DllImportsFromSharedLib_x64_dll.grpcsharp_channel_credentials_release;
            this.grpcsharp_insecure_channel_create = DllImportsFromSharedLib_x64_dll.grpcsharp_insecure_channel_create;
            this.grpcsharp_secure_channel_create = DllImportsFromSharedLib_x64_dll.grpcsharp_secure_channel_create;
            this.grpcsharp_channel_create_call = DllImportsFromSharedLib_x64_dll.grpcsharp_channel_create_call;
            this.grpcsharp_channel_check_connectivity_state = DllImportsFromSharedLib_x64_dll.grpcsharp_channel_check_connectivity_state;
            this.grpcsharp_channel_watch_connectivity_state = DllImportsFromSharedLib_x64_dll.grpcsharp_channel_watch_connectivity_state;
            this.grpcsharp_channel_get_target = DllImportsFromSharedLib_x64_dll.grpcsharp_channel_get_target;
            this.grpcsharp_channel_destroy = DllImportsFromSharedLib_x64_dll.grpcsharp_channel_destroy;
            this.grpcsharp_sizeof_grpc_event = DllImportsFromSharedLib_x64_dll.grpcsharp_sizeof_grpc_event;
            this.grpcsharp_completion_queue_create_async = DllImportsFromSharedLib_x64_dll.grpcsharp_completion_queue_create_async;
            this.grpcsharp_completion_queue_create_sync = DllImportsFromSharedLib_x64_dll.grpcsharp_completion_queue_create_sync;
            this.grpcsharp_completion_queue_shutdown = DllImportsFromSharedLib_x64_dll.grpcsharp_completion_queue_shutdown;
            this.grpcsharp_completion_queue_next = DllImportsFromSharedLib_x64_dll.grpcsharp_completion_queue_next;
            this.grpcsharp_completion_queue_pluck = DllImportsFromSharedLib_x64_dll.grpcsharp_completion_queue_pluck;
            this.grpcsharp_completion_queue_destroy = DllImportsFromSharedLib_x64_dll.grpcsharp_completion_queue_destroy;
            this.gprsharp_free = DllImportsFromSharedLib_x64_dll.gprsharp_free;
            this.grpcsharp_metadata_array_create = DllImportsFromSharedLib_x64_dll.grpcsharp_metadata_array_create;
            this.grpcsharp_metadata_array_add = DllImportsFromSharedLib_x64_dll.grpcsharp_metadata_array_add;
            this.grpcsharp_metadata_array_count = DllImportsFromSharedLib_x64_dll.grpcsharp_metadata_array_count;
            this.grpcsharp_metadata_array_get_key = DllImportsFromSharedLib_x64_dll.grpcsharp_metadata_array_get_key;
            this.grpcsharp_metadata_array_get_value = DllImportsFromSharedLib_x64_dll.grpcsharp_metadata_array_get_value;
            this.grpcsharp_metadata_array_destroy_full = DllImportsFromSharedLib_x64_dll.grpcsharp_metadata_array_destroy_full;
            this.grpcsharp_redirect_log = DllImportsFromSharedLib_x64_dll.grpcsharp_redirect_log;
            this.grpcsharp_native_callback_dispatcher_init = DllImportsFromSharedLib_x64_dll.grpcsharp_native_callback_dispatcher_init;
            this.grpcsharp_metadata_credentials_create_from_plugin = DllImportsFromSharedLib_x64_dll.grpcsharp_metadata_credentials_create_from_plugin;
            this.grpcsharp_metadata_credentials_notify_from_plugin = DllImportsFromSharedLib_x64_dll.grpcsharp_metadata_credentials_notify_from_plugin;
            this.grpcsharp_ssl_server_credentials_create = DllImportsFromSharedLib_x64_dll.grpcsharp_ssl_server_credentials_create;
            this.grpcsharp_server_credentials_release = DllImportsFromSharedLib_x64_dll.grpcsharp_server_credentials_release;
            this.grpcsharp_server_create = DllImportsFromSharedLib_x64_dll.grpcsharp_server_create;
            this.grpcsharp_server_register_completion_queue = DllImportsFromSharedLib_x64_dll.grpcsharp_server_register_completion_queue;
            this.grpcsharp_server_add_insecure_http2_port = DllImportsFromSharedLib_x64_dll.grpcsharp_server_add_insecure_http2_port;
            this.grpcsharp_server_add_secure_http2_port = DllImportsFromSharedLib_x64_dll.grpcsharp_server_add_secure_http2_port;
            this.grpcsharp_server_start = DllImportsFromSharedLib_x64_dll.grpcsharp_server_start;
            this.grpcsharp_server_request_call = DllImportsFromSharedLib_x64_dll.grpcsharp_server_request_call;
            this.grpcsharp_server_cancel_all_calls = DllImportsFromSharedLib_x64_dll.grpcsharp_server_cancel_all_calls;
            this.grpcsharp_server_shutdown_and_notify_callback = DllImportsFromSharedLib_x64_dll.grpcsharp_server_shutdown_and_notify_callback;
            this.grpcsharp_server_destroy = DllImportsFromSharedLib_x64_dll.grpcsharp_server_destroy;
            this.grpcsharp_call_auth_context = DllImportsFromSharedLib_x64_dll.grpcsharp_call_auth_context;
            this.grpcsharp_auth_context_peer_identity_property_name = DllImportsFromSharedLib_x64_dll.grpcsharp_auth_context_peer_identity_property_name;
            this.grpcsharp_auth_context_property_iterator = DllImportsFromSharedLib_x64_dll.grpcsharp_auth_context_property_iterator;
            this.grpcsharp_auth_property_iterator_next = DllImportsFromSharedLib_x64_dll.grpcsharp_auth_property_iterator_next;
            this.grpcsharp_auth_context_release = DllImportsFromSharedLib_x64_dll.grpcsharp_auth_context_release;
            this.grpcsharp_slice_buffer_create = DllImportsFromSharedLib_x64_dll.grpcsharp_slice_buffer_create;
            this.grpcsharp_slice_buffer_adjust_tail_space = DllImportsFromSharedLib_x64_dll.grpcsharp_slice_buffer_adjust_tail_space;
            this.grpcsharp_slice_buffer_slice_count = DllImportsFromSharedLib_x64_dll.grpcsharp_slice_buffer_slice_count;
            this.grpcsharp_slice_buffer_slice_peek = DllImportsFromSharedLib_x64_dll.grpcsharp_slice_buffer_slice_peek;
            this.grpcsharp_slice_buffer_reset_and_unref = DllImportsFromSharedLib_x64_dll.grpcsharp_slice_buffer_reset_and_unref;
            this.grpcsharp_slice_buffer_destroy = DllImportsFromSharedLib_x64_dll.grpcsharp_slice_buffer_destroy;
            this.gprsharp_now = DllImportsFromSharedLib_x64_dll.gprsharp_now;
            this.gprsharp_inf_future = DllImportsFromSharedLib_x64_dll.gprsharp_inf_future;
            this.gprsharp_inf_past = DllImportsFromSharedLib_x64_dll.gprsharp_inf_past;
            this.gprsharp_convert_clock_type = DllImportsFromSharedLib_x64_dll.gprsharp_convert_clock_type;
            this.gprsharp_sizeof_timespec = DllImportsFromSharedLib_x64_dll.gprsharp_sizeof_timespec;
            this.grpcsharp_test_callback = DllImportsFromSharedLib_x64_dll.grpcsharp_test_callback;
            this.grpcsharp_test_nop = DllImportsFromSharedLib_x64_dll.grpcsharp_test_nop;
            this.grpcsharp_test_override_method = DllImportsFromSharedLib_x64_dll.grpcsharp_test_override_method;
            this.grpcsharp_test_call_start_unary_echo = DllImportsFromSharedLib_x64_dll.grpcsharp_test_call_start_unary_echo;
        }

        /// <summary>
        /// Delegate types for all published native methods. Declared under inner class to prevent scope pollution.
        /// </summary>
        public class Delegates
        {
            public delegate void grpcsharp_init_delegate();
            public delegate void grpcsharp_shutdown_delegate();
            public delegate IntPtr grpcsharp_version_string_delegate();  // returns not-owned const char*
            public delegate BatchContextSafeHandle grpcsharp_batch_context_create_delegate();
            public delegate IntPtr grpcsharp_batch_context_recv_initial_metadata_delegate(BatchContextSafeHandle ctx);
            public delegate IntPtr grpcsharp_batch_context_recv_message_length_delegate(BatchContextSafeHandle ctx);
            public delegate int grpcsharp_batch_context_recv_message_next_slice_peek_delegate(BatchContextSafeHandle ctx, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            public delegate StatusCode grpcsharp_batch_context_recv_status_on_client_status_delegate(BatchContextSafeHandle ctx);
            public delegate IntPtr grpcsharp_batch_context_recv_status_on_client_details_delegate(BatchContextSafeHandle ctx, out UIntPtr detailsLength);
            public delegate IntPtr grpcsharp_batch_context_recv_status_on_client_error_string_delegate(BatchContextSafeHandle ctx);
            public delegate IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata_delegate(BatchContextSafeHandle ctx);
            public delegate int grpcsharp_batch_context_recv_close_on_server_cancelled_delegate(BatchContextSafeHandle ctx);
            public delegate void grpcsharp_batch_context_reset_delegate(BatchContextSafeHandle ctx);
            public delegate void grpcsharp_batch_context_destroy_delegate(IntPtr ctx);
            public delegate RequestCallContextSafeHandle grpcsharp_request_call_context_create_delegate();
            public delegate CallSafeHandle grpcsharp_request_call_context_call_delegate(RequestCallContextSafeHandle ctx);
            public delegate IntPtr grpcsharp_request_call_context_method_delegate(RequestCallContextSafeHandle ctx, out UIntPtr methodLength);
            public delegate IntPtr grpcsharp_request_call_context_host_delegate(RequestCallContextSafeHandle ctx, out UIntPtr hostLength);
            public delegate Timespec grpcsharp_request_call_context_deadline_delegate(RequestCallContextSafeHandle ctx);
            public delegate IntPtr grpcsharp_request_call_context_request_metadata_delegate(RequestCallContextSafeHandle ctx);
            public delegate void grpcsharp_request_call_context_reset_delegate(RequestCallContextSafeHandle ctx);
            public delegate void grpcsharp_request_call_context_destroy_delegate(IntPtr ctx);
            public delegate CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create_delegate(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);
            public delegate void grpcsharp_call_credentials_release_delegate(IntPtr credentials);
            public delegate CallError grpcsharp_call_cancel_delegate(CallSafeHandle call);
            public delegate CallError grpcsharp_call_cancel_with_status_delegate(CallSafeHandle call, StatusCode status, string description);
            public delegate CallError grpcsharp_call_start_unary_delegate(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            public delegate CallError grpcsharp_call_start_client_streaming_delegate(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            public delegate CallError grpcsharp_call_start_server_streaming_delegate(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            public delegate CallError grpcsharp_call_start_duplex_streaming_delegate(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            public delegate CallError grpcsharp_call_send_message_delegate(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, int sendEmptyInitialMetadata);
            public delegate CallError grpcsharp_call_send_close_from_client_delegate(CallSafeHandle call, BatchContextSafeHandle ctx);
            public delegate CallError grpcsharp_call_send_status_from_server_delegate(CallSafeHandle call, BatchContextSafeHandle ctx, StatusCode statusCode, IntPtr statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata, SliceBufferSafeHandle optionalSendBuffer, WriteFlags writeFlags);
            public delegate CallError grpcsharp_call_recv_message_delegate(CallSafeHandle call, BatchContextSafeHandle ctx);
            public delegate CallError grpcsharp_call_recv_initial_metadata_delegate(CallSafeHandle call, BatchContextSafeHandle ctx);
            public delegate CallError grpcsharp_call_start_serverside_delegate(CallSafeHandle call, BatchContextSafeHandle ctx);
            public delegate CallError grpcsharp_call_send_initial_metadata_delegate(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            public delegate CallError grpcsharp_call_set_credentials_delegate(CallSafeHandle call, CallCredentialsSafeHandle credentials);
            public delegate CStringSafeHandle grpcsharp_call_get_peer_delegate(CallSafeHandle call);
            public delegate void grpcsharp_call_destroy_delegate(IntPtr call);
            public delegate ChannelArgsSafeHandle grpcsharp_channel_args_create_delegate(UIntPtr numArgs);
            public delegate void grpcsharp_channel_args_set_string_delegate(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);
            public delegate void grpcsharp_channel_args_set_integer_delegate(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);
            public delegate void grpcsharp_channel_args_destroy_delegate(IntPtr args);
            public delegate void grpcsharp_override_default_ssl_roots_delegate(string pemRootCerts);
            public delegate ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create_delegate(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey, IntPtr verifyPeerCallbackTag);
            public delegate ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create_delegate(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);
            public delegate void grpcsharp_channel_credentials_release_delegate(IntPtr credentials);
            public delegate ChannelSafeHandle grpcsharp_insecure_channel_create_delegate(string target, ChannelArgsSafeHandle channelArgs);
            public delegate ChannelSafeHandle grpcsharp_secure_channel_create_delegate(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);
            public delegate CallSafeHandle grpcsharp_channel_create_call_delegate(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);
            public delegate ChannelState grpcsharp_channel_check_connectivity_state_delegate(ChannelSafeHandle channel, int tryToConnect);
            public delegate void grpcsharp_channel_watch_connectivity_state_delegate(ChannelSafeHandle channel, ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            public delegate CStringSafeHandle grpcsharp_channel_get_target_delegate(ChannelSafeHandle call);
            public delegate void grpcsharp_channel_destroy_delegate(IntPtr channel);
            public delegate int grpcsharp_sizeof_grpc_event_delegate();
            public delegate CompletionQueueSafeHandle grpcsharp_completion_queue_create_async_delegate();
            public delegate CompletionQueueSafeHandle grpcsharp_completion_queue_create_sync_delegate();
            public delegate void grpcsharp_completion_queue_shutdown_delegate(CompletionQueueSafeHandle cq);
            public delegate CompletionQueueEvent grpcsharp_completion_queue_next_delegate(CompletionQueueSafeHandle cq);
            public delegate CompletionQueueEvent grpcsharp_completion_queue_pluck_delegate(CompletionQueueSafeHandle cq, IntPtr tag);
            public delegate void grpcsharp_completion_queue_destroy_delegate(IntPtr cq);
            public delegate void gprsharp_free_delegate(IntPtr ptr);
            public delegate MetadataArraySafeHandle grpcsharp_metadata_array_create_delegate(UIntPtr capacity);
            public delegate void grpcsharp_metadata_array_add_delegate(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);
            public delegate UIntPtr grpcsharp_metadata_array_count_delegate(IntPtr metadataArray);
            public delegate IntPtr grpcsharp_metadata_array_get_key_delegate(IntPtr metadataArray, UIntPtr index, out UIntPtr keyLength);
            public delegate IntPtr grpcsharp_metadata_array_get_value_delegate(IntPtr metadataArray, UIntPtr index, out UIntPtr valueLength);
            public delegate void grpcsharp_metadata_array_destroy_full_delegate(IntPtr array);
            public delegate void grpcsharp_redirect_log_delegate(GprLogDelegate callback);
            public delegate void grpcsharp_native_callback_dispatcher_init_delegate(NativeCallbackDispatcherCallback dispatcher);
            public delegate CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin_delegate(IntPtr nativeCallbackTag);
            public delegate void grpcsharp_metadata_credentials_notify_from_plugin_delegate(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);
            public delegate ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create_delegate(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, SslClientCertificateRequestType clientCertificateRequest);
            public delegate void grpcsharp_server_credentials_release_delegate(IntPtr credentials);
            public delegate ServerSafeHandle grpcsharp_server_create_delegate(ChannelArgsSafeHandle args);
            public delegate void grpcsharp_server_register_completion_queue_delegate(ServerSafeHandle server, CompletionQueueSafeHandle cq);
            public delegate int grpcsharp_server_add_insecure_http2_port_delegate(ServerSafeHandle server, string addr);
            public delegate int grpcsharp_server_add_secure_http2_port_delegate(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);
            public delegate void grpcsharp_server_start_delegate(ServerSafeHandle server);
            public delegate CallError grpcsharp_server_request_call_delegate(ServerSafeHandle server, CompletionQueueSafeHandle cq, RequestCallContextSafeHandle ctx);
            public delegate void grpcsharp_server_cancel_all_calls_delegate(ServerSafeHandle server);
            public delegate void grpcsharp_server_shutdown_and_notify_callback_delegate(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            public delegate void grpcsharp_server_destroy_delegate(IntPtr server);
            public delegate AuthContextSafeHandle grpcsharp_call_auth_context_delegate(CallSafeHandle call);
            public delegate IntPtr grpcsharp_auth_context_peer_identity_property_name_delegate(AuthContextSafeHandle authContext);  // returns const char*
            public delegate AuthContextSafeHandle.NativeAuthPropertyIterator grpcsharp_auth_context_property_iterator_delegate(AuthContextSafeHandle authContext);
            public delegate IntPtr grpcsharp_auth_property_iterator_next_delegate(ref AuthContextSafeHandle.NativeAuthPropertyIterator iterator);  // returns const auth_property*
            public delegate void grpcsharp_auth_context_release_delegate(IntPtr authContext);
            public delegate SliceBufferSafeHandle grpcsharp_slice_buffer_create_delegate();
            public delegate IntPtr grpcsharp_slice_buffer_adjust_tail_space_delegate(SliceBufferSafeHandle sliceBuffer, UIntPtr availableTailSpace, UIntPtr requestedTailSpace);
            public delegate UIntPtr grpcsharp_slice_buffer_slice_count_delegate(SliceBufferSafeHandle sliceBuffer);
            public delegate void grpcsharp_slice_buffer_slice_peek_delegate(SliceBufferSafeHandle sliceBuffer, UIntPtr index, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            public delegate void grpcsharp_slice_buffer_reset_and_unref_delegate(SliceBufferSafeHandle sliceBuffer);
            public delegate void grpcsharp_slice_buffer_destroy_delegate(IntPtr sliceBuffer);
            public delegate Timespec gprsharp_now_delegate(ClockType clockType);
            public delegate Timespec gprsharp_inf_future_delegate(ClockType clockType);
            public delegate Timespec gprsharp_inf_past_delegate(ClockType clockType);
            public delegate Timespec gprsharp_convert_clock_type_delegate(Timespec t, ClockType targetClock);
            public delegate int gprsharp_sizeof_timespec_delegate();
            public delegate CallError grpcsharp_test_callback_delegate([MarshalAs(UnmanagedType.FunctionPtr)] NativeCallbackTestDelegate callback);
            public delegate IntPtr grpcsharp_test_nop_delegate(IntPtr ptr);
            public delegate void grpcsharp_test_override_method_delegate(string methodName, string variant);
            public delegate CallError grpcsharp_test_call_start_unary_echo_delegate(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
        }
        
        /// <summary>
        /// grpc_csharp_ext used as a static library (e.g Unity iOS).
        /// </summary>
        internal class DllImportsFromStaticLib
        {
            private const string ImportName = "__Internal";
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_init();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_shutdown();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_version_string();
            
            [DllImport(ImportName)]
            public static extern BatchContextSafeHandle grpcsharp_batch_context_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_message_next_slice_peek(BatchContextSafeHandle ctx, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx, out UIntPtr detailsLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_error_string(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_reset(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern RequestCallContextSafeHandle grpcsharp_request_call_context_create();
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_request_call_context_call(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_method(RequestCallContextSafeHandle ctx, out UIntPtr methodLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_host(RequestCallContextSafeHandle ctx, out UIntPtr hostLength);
            
            [DllImport(ImportName)]
            public static extern Timespec grpcsharp_request_call_context_deadline(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_request_metadata(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_reset(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_unary(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_client_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_server_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_message(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, int sendEmptyInitialMetadata);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_close_from_client(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_status_from_server(CallSafeHandle call, BatchContextSafeHandle ctx, StatusCode statusCode, IntPtr statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata, SliceBufferSafeHandle optionalSendBuffer, WriteFlags writeFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_message(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_serverside(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_destroy(IntPtr call);
            
            [DllImport(ImportName)]
            public static extern ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_destroy(IntPtr args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_override_default_ssl_roots(string pemRootCerts);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey, IntPtr verifyPeerCallbackTag);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);
            
            [DllImport(ImportName)]
            public static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_destroy(IntPtr channel);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_sizeof_grpc_event();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_async();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_sync();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_destroy(IntPtr cq);
            
            [DllImport(ImportName)]
            public static extern void gprsharp_free(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index, out UIntPtr keyLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index, out UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_destroy_full(IntPtr array);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_redirect_log(GprLogDelegate callback);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_native_callback_dispatcher_init(NativeCallbackDispatcherCallback dispatcher);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(IntPtr nativeCallbackTag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);
            
            [DllImport(ImportName)]
            public static extern ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, SslClientCertificateRequestType clientCertificateRequest);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ServerSafeHandle grpcsharp_server_create(ChannelArgsSafeHandle args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_register_completion_queue(ServerSafeHandle server, CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_start(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_destroy(IntPtr server);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle grpcsharp_call_auth_context(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_context_peer_identity_property_name(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle.NativeAuthPropertyIterator grpcsharp_auth_context_property_iterator(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_property_iterator_next(ref AuthContextSafeHandle.NativeAuthPropertyIterator iterator);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_auth_context_release(IntPtr authContext);
            
            [DllImport(ImportName)]
            public static extern SliceBufferSafeHandle grpcsharp_slice_buffer_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_slice_buffer_adjust_tail_space(SliceBufferSafeHandle sliceBuffer, UIntPtr availableTailSpace, UIntPtr requestedTailSpace);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_slice_buffer_slice_count(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_slice_peek(SliceBufferSafeHandle sliceBuffer, UIntPtr index, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_reset_and_unref(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_destroy(IntPtr sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_now(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_future(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_past(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_convert_clock_type(Timespec t, ClockType targetClock);
            
            [DllImport(ImportName)]
            public static extern int gprsharp_sizeof_timespec();
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_callback([MarshalAs(UnmanagedType.FunctionPtr)] NativeCallbackTestDelegate callback);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_test_nop(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_test_override_method(string methodName, string variant);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_call_start_unary_echo(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
        }
        
        /// <summary>
        /// grpc_csharp_ext used as a shared library (e.g on Unity Standalone and Android).
        /// </summary>
        internal class DllImportsFromSharedLib
        {
            private const string ImportName = "grpc_csharp_ext";
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_init();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_shutdown();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_version_string();
            
            [DllImport(ImportName)]
            public static extern BatchContextSafeHandle grpcsharp_batch_context_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_message_next_slice_peek(BatchContextSafeHandle ctx, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx, out UIntPtr detailsLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_error_string(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_reset(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern RequestCallContextSafeHandle grpcsharp_request_call_context_create();
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_request_call_context_call(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_method(RequestCallContextSafeHandle ctx, out UIntPtr methodLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_host(RequestCallContextSafeHandle ctx, out UIntPtr hostLength);
            
            [DllImport(ImportName)]
            public static extern Timespec grpcsharp_request_call_context_deadline(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_request_metadata(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_reset(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_unary(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_client_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_server_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_message(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, int sendEmptyInitialMetadata);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_close_from_client(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_status_from_server(CallSafeHandle call, BatchContextSafeHandle ctx, StatusCode statusCode, IntPtr statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata, SliceBufferSafeHandle optionalSendBuffer, WriteFlags writeFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_message(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_serverside(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_destroy(IntPtr call);
            
            [DllImport(ImportName)]
            public static extern ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_destroy(IntPtr args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_override_default_ssl_roots(string pemRootCerts);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey, IntPtr verifyPeerCallbackTag);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);
            
            [DllImport(ImportName)]
            public static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_destroy(IntPtr channel);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_sizeof_grpc_event();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_async();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_sync();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_destroy(IntPtr cq);
            
            [DllImport(ImportName)]
            public static extern void gprsharp_free(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index, out UIntPtr keyLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index, out UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_destroy_full(IntPtr array);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_redirect_log(GprLogDelegate callback);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_native_callback_dispatcher_init(NativeCallbackDispatcherCallback dispatcher);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(IntPtr nativeCallbackTag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);
            
            [DllImport(ImportName)]
            public static extern ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, SslClientCertificateRequestType clientCertificateRequest);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ServerSafeHandle grpcsharp_server_create(ChannelArgsSafeHandle args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_register_completion_queue(ServerSafeHandle server, CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_start(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_destroy(IntPtr server);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle grpcsharp_call_auth_context(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_context_peer_identity_property_name(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle.NativeAuthPropertyIterator grpcsharp_auth_context_property_iterator(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_property_iterator_next(ref AuthContextSafeHandle.NativeAuthPropertyIterator iterator);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_auth_context_release(IntPtr authContext);
            
            [DllImport(ImportName)]
            public static extern SliceBufferSafeHandle grpcsharp_slice_buffer_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_slice_buffer_adjust_tail_space(SliceBufferSafeHandle sliceBuffer, UIntPtr availableTailSpace, UIntPtr requestedTailSpace);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_slice_buffer_slice_count(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_slice_peek(SliceBufferSafeHandle sliceBuffer, UIntPtr index, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_reset_and_unref(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_destroy(IntPtr sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_now(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_future(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_past(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_convert_clock_type(Timespec t, ClockType targetClock);
            
            [DllImport(ImportName)]
            public static extern int gprsharp_sizeof_timespec();
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_callback([MarshalAs(UnmanagedType.FunctionPtr)] NativeCallbackTestDelegate callback);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_test_nop(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_test_override_method(string methodName, string variant);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_call_start_unary_echo(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
        }

        /// <summary>
        /// grpc_csharp_ext used as a shared library (with x86 suffix)
        /// </summary>
        internal class DllImportsFromSharedLib_x86
        {
            private const string ImportName = "grpc_csharp_ext.x86";
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_init();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_shutdown();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_version_string();
            
            [DllImport(ImportName)]
            public static extern BatchContextSafeHandle grpcsharp_batch_context_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_message_next_slice_peek(BatchContextSafeHandle ctx, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx, out UIntPtr detailsLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_error_string(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_reset(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern RequestCallContextSafeHandle grpcsharp_request_call_context_create();
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_request_call_context_call(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_method(RequestCallContextSafeHandle ctx, out UIntPtr methodLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_host(RequestCallContextSafeHandle ctx, out UIntPtr hostLength);
            
            [DllImport(ImportName)]
            public static extern Timespec grpcsharp_request_call_context_deadline(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_request_metadata(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_reset(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_unary(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_client_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_server_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_message(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, int sendEmptyInitialMetadata);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_close_from_client(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_status_from_server(CallSafeHandle call, BatchContextSafeHandle ctx, StatusCode statusCode, IntPtr statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata, SliceBufferSafeHandle optionalSendBuffer, WriteFlags writeFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_message(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_serverside(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_destroy(IntPtr call);
            
            [DllImport(ImportName)]
            public static extern ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_destroy(IntPtr args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_override_default_ssl_roots(string pemRootCerts);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey, IntPtr verifyPeerCallbackTag);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);
            
            [DllImport(ImportName)]
            public static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_destroy(IntPtr channel);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_sizeof_grpc_event();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_async();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_sync();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_destroy(IntPtr cq);
            
            [DllImport(ImportName)]
            public static extern void gprsharp_free(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index, out UIntPtr keyLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index, out UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_destroy_full(IntPtr array);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_redirect_log(GprLogDelegate callback);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_native_callback_dispatcher_init(NativeCallbackDispatcherCallback dispatcher);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(IntPtr nativeCallbackTag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);
            
            [DllImport(ImportName)]
            public static extern ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, SslClientCertificateRequestType clientCertificateRequest);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ServerSafeHandle grpcsharp_server_create(ChannelArgsSafeHandle args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_register_completion_queue(ServerSafeHandle server, CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_start(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_destroy(IntPtr server);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle grpcsharp_call_auth_context(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_context_peer_identity_property_name(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle.NativeAuthPropertyIterator grpcsharp_auth_context_property_iterator(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_property_iterator_next(ref AuthContextSafeHandle.NativeAuthPropertyIterator iterator);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_auth_context_release(IntPtr authContext);
            
            [DllImport(ImportName)]
            public static extern SliceBufferSafeHandle grpcsharp_slice_buffer_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_slice_buffer_adjust_tail_space(SliceBufferSafeHandle sliceBuffer, UIntPtr availableTailSpace, UIntPtr requestedTailSpace);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_slice_buffer_slice_count(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_slice_peek(SliceBufferSafeHandle sliceBuffer, UIntPtr index, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_reset_and_unref(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_destroy(IntPtr sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_now(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_future(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_past(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_convert_clock_type(Timespec t, ClockType targetClock);
            
            [DllImport(ImportName)]
            public static extern int gprsharp_sizeof_timespec();
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_callback([MarshalAs(UnmanagedType.FunctionPtr)] NativeCallbackTestDelegate callback);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_test_nop(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_test_override_method(string methodName, string variant);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_call_start_unary_echo(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
        }

        /// <summary>
        /// grpc_csharp_ext used as a shared library (with x64 suffix)
        /// </summary>
        internal class DllImportsFromSharedLib_x64
        {
            private const string ImportName = "grpc_csharp_ext.x64";
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_init();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_shutdown();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_version_string();
            
            [DllImport(ImportName)]
            public static extern BatchContextSafeHandle grpcsharp_batch_context_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_message_next_slice_peek(BatchContextSafeHandle ctx, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx, out UIntPtr detailsLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_error_string(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_reset(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern RequestCallContextSafeHandle grpcsharp_request_call_context_create();
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_request_call_context_call(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_method(RequestCallContextSafeHandle ctx, out UIntPtr methodLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_host(RequestCallContextSafeHandle ctx, out UIntPtr hostLength);
            
            [DllImport(ImportName)]
            public static extern Timespec grpcsharp_request_call_context_deadline(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_request_metadata(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_reset(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_unary(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_client_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_server_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_message(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, int sendEmptyInitialMetadata);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_close_from_client(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_status_from_server(CallSafeHandle call, BatchContextSafeHandle ctx, StatusCode statusCode, IntPtr statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata, SliceBufferSafeHandle optionalSendBuffer, WriteFlags writeFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_message(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_serverside(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_destroy(IntPtr call);
            
            [DllImport(ImportName)]
            public static extern ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_destroy(IntPtr args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_override_default_ssl_roots(string pemRootCerts);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey, IntPtr verifyPeerCallbackTag);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);
            
            [DllImport(ImportName)]
            public static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_destroy(IntPtr channel);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_sizeof_grpc_event();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_async();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_sync();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_destroy(IntPtr cq);
            
            [DllImport(ImportName)]
            public static extern void gprsharp_free(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index, out UIntPtr keyLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index, out UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_destroy_full(IntPtr array);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_redirect_log(GprLogDelegate callback);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_native_callback_dispatcher_init(NativeCallbackDispatcherCallback dispatcher);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(IntPtr nativeCallbackTag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);
            
            [DllImport(ImportName)]
            public static extern ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, SslClientCertificateRequestType clientCertificateRequest);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ServerSafeHandle grpcsharp_server_create(ChannelArgsSafeHandle args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_register_completion_queue(ServerSafeHandle server, CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_start(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_destroy(IntPtr server);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle grpcsharp_call_auth_context(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_context_peer_identity_property_name(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle.NativeAuthPropertyIterator grpcsharp_auth_context_property_iterator(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_property_iterator_next(ref AuthContextSafeHandle.NativeAuthPropertyIterator iterator);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_auth_context_release(IntPtr authContext);
            
            [DllImport(ImportName)]
            public static extern SliceBufferSafeHandle grpcsharp_slice_buffer_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_slice_buffer_adjust_tail_space(SliceBufferSafeHandle sliceBuffer, UIntPtr availableTailSpace, UIntPtr requestedTailSpace);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_slice_buffer_slice_count(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_slice_peek(SliceBufferSafeHandle sliceBuffer, UIntPtr index, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_reset_and_unref(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_destroy(IntPtr sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_now(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_future(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_past(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_convert_clock_type(Timespec t, ClockType targetClock);
            
            [DllImport(ImportName)]
            public static extern int gprsharp_sizeof_timespec();
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_callback([MarshalAs(UnmanagedType.FunctionPtr)] NativeCallbackTestDelegate callback);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_test_nop(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_test_override_method(string methodName, string variant);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_call_start_unary_echo(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
        }

        /// <summary>
        /// grpc_csharp_ext used as a shared library (with arm64 suffix)
        /// </summary>
        internal class DllImportsFromSharedLib_arm64
        {
            private const string ImportName = "grpc_csharp_ext.arm64";
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_init();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_shutdown();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_version_string();
            
            [DllImport(ImportName)]
            public static extern BatchContextSafeHandle grpcsharp_batch_context_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_message_next_slice_peek(BatchContextSafeHandle ctx, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx, out UIntPtr detailsLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_error_string(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_reset(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern RequestCallContextSafeHandle grpcsharp_request_call_context_create();
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_request_call_context_call(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_method(RequestCallContextSafeHandle ctx, out UIntPtr methodLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_host(RequestCallContextSafeHandle ctx, out UIntPtr hostLength);
            
            [DllImport(ImportName)]
            public static extern Timespec grpcsharp_request_call_context_deadline(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_request_metadata(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_reset(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_unary(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_client_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_server_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_message(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, int sendEmptyInitialMetadata);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_close_from_client(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_status_from_server(CallSafeHandle call, BatchContextSafeHandle ctx, StatusCode statusCode, IntPtr statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata, SliceBufferSafeHandle optionalSendBuffer, WriteFlags writeFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_message(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_serverside(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_destroy(IntPtr call);
            
            [DllImport(ImportName)]
            public static extern ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_destroy(IntPtr args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_override_default_ssl_roots(string pemRootCerts);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey, IntPtr verifyPeerCallbackTag);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);
            
            [DllImport(ImportName)]
            public static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_destroy(IntPtr channel);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_sizeof_grpc_event();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_async();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_sync();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_destroy(IntPtr cq);
            
            [DllImport(ImportName)]
            public static extern void gprsharp_free(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index, out UIntPtr keyLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index, out UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_destroy_full(IntPtr array);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_redirect_log(GprLogDelegate callback);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_native_callback_dispatcher_init(NativeCallbackDispatcherCallback dispatcher);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(IntPtr nativeCallbackTag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);
            
            [DllImport(ImportName)]
            public static extern ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, SslClientCertificateRequestType clientCertificateRequest);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ServerSafeHandle grpcsharp_server_create(ChannelArgsSafeHandle args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_register_completion_queue(ServerSafeHandle server, CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_start(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_destroy(IntPtr server);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle grpcsharp_call_auth_context(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_context_peer_identity_property_name(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle.NativeAuthPropertyIterator grpcsharp_auth_context_property_iterator(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_property_iterator_next(ref AuthContextSafeHandle.NativeAuthPropertyIterator iterator);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_auth_context_release(IntPtr authContext);
            
            [DllImport(ImportName)]
            public static extern SliceBufferSafeHandle grpcsharp_slice_buffer_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_slice_buffer_adjust_tail_space(SliceBufferSafeHandle sliceBuffer, UIntPtr availableTailSpace, UIntPtr requestedTailSpace);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_slice_buffer_slice_count(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_slice_peek(SliceBufferSafeHandle sliceBuffer, UIntPtr index, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_reset_and_unref(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_destroy(IntPtr sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_now(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_future(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_past(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_convert_clock_type(Timespec t, ClockType targetClock);
            
            [DllImport(ImportName)]
            public static extern int gprsharp_sizeof_timespec();
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_callback([MarshalAs(UnmanagedType.FunctionPtr)] NativeCallbackTestDelegate callback);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_test_nop(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_test_override_method(string methodName, string variant);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_call_start_unary_echo(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
        }

        /// <summary>
        /// grpc_csharp_ext used as a shared library (with x86.dll suffix)
        /// </summary>
        internal class DllImportsFromSharedLib_x86_dll
        {
            private const string ImportName = "grpc_csharp_ext.x86.dll";
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_init();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_shutdown();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_version_string();
            
            [DllImport(ImportName)]
            public static extern BatchContextSafeHandle grpcsharp_batch_context_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_message_next_slice_peek(BatchContextSafeHandle ctx, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx, out UIntPtr detailsLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_error_string(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_reset(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern RequestCallContextSafeHandle grpcsharp_request_call_context_create();
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_request_call_context_call(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_method(RequestCallContextSafeHandle ctx, out UIntPtr methodLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_host(RequestCallContextSafeHandle ctx, out UIntPtr hostLength);
            
            [DllImport(ImportName)]
            public static extern Timespec grpcsharp_request_call_context_deadline(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_request_metadata(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_reset(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_unary(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_client_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_server_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_message(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, int sendEmptyInitialMetadata);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_close_from_client(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_status_from_server(CallSafeHandle call, BatchContextSafeHandle ctx, StatusCode statusCode, IntPtr statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata, SliceBufferSafeHandle optionalSendBuffer, WriteFlags writeFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_message(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_serverside(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_destroy(IntPtr call);
            
            [DllImport(ImportName)]
            public static extern ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_destroy(IntPtr args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_override_default_ssl_roots(string pemRootCerts);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey, IntPtr verifyPeerCallbackTag);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);
            
            [DllImport(ImportName)]
            public static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_destroy(IntPtr channel);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_sizeof_grpc_event();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_async();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_sync();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_destroy(IntPtr cq);
            
            [DllImport(ImportName)]
            public static extern void gprsharp_free(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index, out UIntPtr keyLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index, out UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_destroy_full(IntPtr array);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_redirect_log(GprLogDelegate callback);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_native_callback_dispatcher_init(NativeCallbackDispatcherCallback dispatcher);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(IntPtr nativeCallbackTag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);
            
            [DllImport(ImportName)]
            public static extern ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, SslClientCertificateRequestType clientCertificateRequest);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ServerSafeHandle grpcsharp_server_create(ChannelArgsSafeHandle args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_register_completion_queue(ServerSafeHandle server, CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_start(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_destroy(IntPtr server);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle grpcsharp_call_auth_context(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_context_peer_identity_property_name(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle.NativeAuthPropertyIterator grpcsharp_auth_context_property_iterator(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_property_iterator_next(ref AuthContextSafeHandle.NativeAuthPropertyIterator iterator);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_auth_context_release(IntPtr authContext);
            
            [DllImport(ImportName)]
            public static extern SliceBufferSafeHandle grpcsharp_slice_buffer_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_slice_buffer_adjust_tail_space(SliceBufferSafeHandle sliceBuffer, UIntPtr availableTailSpace, UIntPtr requestedTailSpace);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_slice_buffer_slice_count(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_slice_peek(SliceBufferSafeHandle sliceBuffer, UIntPtr index, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_reset_and_unref(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_destroy(IntPtr sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_now(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_future(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_past(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_convert_clock_type(Timespec t, ClockType targetClock);
            
            [DllImport(ImportName)]
            public static extern int gprsharp_sizeof_timespec();
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_callback([MarshalAs(UnmanagedType.FunctionPtr)] NativeCallbackTestDelegate callback);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_test_nop(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_test_override_method(string methodName, string variant);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_call_start_unary_echo(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
        }

        /// <summary>
        /// grpc_csharp_ext used as a shared library (with x64.dll suffix)
        /// </summary>
        internal class DllImportsFromSharedLib_x64_dll
        {
            private const string ImportName = "grpc_csharp_ext.x64.dll";
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_init();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_shutdown();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_version_string();
            
            [DllImport(ImportName)]
            public static extern BatchContextSafeHandle grpcsharp_batch_context_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_message_next_slice_peek(BatchContextSafeHandle ctx, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx, out UIntPtr detailsLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_error_string(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_reset(BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_batch_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern RequestCallContextSafeHandle grpcsharp_request_call_context_create();
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_request_call_context_call(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_method(RequestCallContextSafeHandle ctx, out UIntPtr methodLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_host(RequestCallContextSafeHandle ctx, out UIntPtr hostLength);
            
            [DllImport(ImportName)]
            public static extern Timespec grpcsharp_request_call_context_deadline(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_request_call_context_request_metadata(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_reset(RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_request_call_context_destroy(IntPtr ctx);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_unary(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_client_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_server_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_message(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, int sendEmptyInitialMetadata);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_close_from_client(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_status_from_server(CallSafeHandle call, BatchContextSafeHandle ctx, StatusCode statusCode, IntPtr statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata, SliceBufferSafeHandle optionalSendBuffer, WriteFlags writeFlags);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_message(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_start_serverside(CallSafeHandle call, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_send_initial_metadata(CallSafeHandle call, BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_call_destroy(IntPtr call);
            
            [DllImport(ImportName)]
            public static extern ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_args_destroy(IntPtr args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_override_default_ssl_roots(string pemRootCerts);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey, IntPtr verifyPeerCallbackTag);
            
            [DllImport(ImportName)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);
            
            [DllImport(ImportName)]
            public static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);
            
            [DllImport(ImportName)]
            public static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_channel_destroy(IntPtr channel);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_sizeof_grpc_event();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_async();
            
            [DllImport(ImportName)]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_sync();
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_completion_queue_destroy(IntPtr cq);
            
            [DllImport(ImportName)]
            public static extern void gprsharp_free(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index, out UIntPtr keyLength);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index, out UIntPtr valueLength);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_array_destroy_full(IntPtr array);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_redirect_log(GprLogDelegate callback);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_native_callback_dispatcher_init(NativeCallbackDispatcherCallback dispatcher);
            
            [DllImport(ImportName)]
            public static extern CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(IntPtr nativeCallbackTag);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);
            
            [DllImport(ImportName)]
            public static extern ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, SslClientCertificateRequestType clientCertificateRequest);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_credentials_release(IntPtr credentials);
            
            [DllImport(ImportName)]
            public static extern ServerSafeHandle grpcsharp_server_create(ChannelArgsSafeHandle args);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_register_completion_queue(ServerSafeHandle server, CompletionQueueSafeHandle cq);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);
            
            [DllImport(ImportName)]
            public static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_start(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, RequestCallContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_server_destroy(IntPtr server);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle grpcsharp_call_auth_context(CallSafeHandle call);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_context_peer_identity_property_name(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern AuthContextSafeHandle.NativeAuthPropertyIterator grpcsharp_auth_context_property_iterator(AuthContextSafeHandle authContext);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_auth_property_iterator_next(ref AuthContextSafeHandle.NativeAuthPropertyIterator iterator);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_auth_context_release(IntPtr authContext);
            
            [DllImport(ImportName)]
            public static extern SliceBufferSafeHandle grpcsharp_slice_buffer_create();
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_slice_buffer_adjust_tail_space(SliceBufferSafeHandle sliceBuffer, UIntPtr availableTailSpace, UIntPtr requestedTailSpace);
            
            [DllImport(ImportName)]
            public static extern UIntPtr grpcsharp_slice_buffer_slice_count(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_slice_peek(SliceBufferSafeHandle sliceBuffer, UIntPtr index, out UIntPtr sliceLen, out IntPtr sliceDataPtr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_reset_and_unref(SliceBufferSafeHandle sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_slice_buffer_destroy(IntPtr sliceBuffer);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_now(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_future(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_inf_past(ClockType clockType);
            
            [DllImport(ImportName)]
            public static extern Timespec gprsharp_convert_clock_type(Timespec t, ClockType targetClock);
            
            [DllImport(ImportName)]
            public static extern int gprsharp_sizeof_timespec();
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_callback([MarshalAs(UnmanagedType.FunctionPtr)] NativeCallbackTestDelegate callback);
            
            [DllImport(ImportName)]
            public static extern IntPtr grpcsharp_test_nop(IntPtr ptr);
            
            [DllImport(ImportName)]
            public static extern void grpcsharp_test_override_method(string methodName, string variant);
            
            [DllImport(ImportName)]
            public static extern CallError grpcsharp_test_call_start_unary_echo(CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
        }
    }
}
