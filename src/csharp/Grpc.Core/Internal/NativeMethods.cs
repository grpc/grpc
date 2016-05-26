#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
    /// <summary>
    /// Provides access to all native methods provided by <c>NativeExtension</c>.
    /// An extra level of indirection is added to P/Invoke calls to allow intelligent loading
    /// of the right configuration of the native extension based on current platform, architecture etc.
    /// </summary>
    internal class NativeMethods
    {
        #region Native methods

        public readonly Delegates.grpcsharp_init_delegate grpcsharp_init;
        public readonly Delegates.grpcsharp_shutdown_delegate grpcsharp_shutdown;
        public readonly Delegates.grpcsharp_version_string_delegate grpcsharp_version_string;

        public readonly Delegates.grpcsharp_batch_context_create_delegate grpcsharp_batch_context_create;
        public readonly Delegates.grpcsharp_batch_context_recv_initial_metadata_delegate grpcsharp_batch_context_recv_initial_metadata;
        public readonly Delegates.grpcsharp_batch_context_recv_message_length_delegate grpcsharp_batch_context_recv_message_length;
        public readonly Delegates.grpcsharp_batch_context_recv_message_to_buffer_delegate grpcsharp_batch_context_recv_message_to_buffer;
        public readonly Delegates.grpcsharp_batch_context_recv_status_on_client_status_delegate grpcsharp_batch_context_recv_status_on_client_status;
        public readonly Delegates.grpcsharp_batch_context_recv_status_on_client_details_delegate grpcsharp_batch_context_recv_status_on_client_details;
        public readonly Delegates.grpcsharp_batch_context_recv_status_on_client_trailing_metadata_delegate grpcsharp_batch_context_recv_status_on_client_trailing_metadata;
        public readonly Delegates.grpcsharp_batch_context_server_rpc_new_call_delegate grpcsharp_batch_context_server_rpc_new_call;
        public readonly Delegates.grpcsharp_batch_context_server_rpc_new_method_delegate grpcsharp_batch_context_server_rpc_new_method;
        public readonly Delegates.grpcsharp_batch_context_server_rpc_new_host_delegate grpcsharp_batch_context_server_rpc_new_host;
        public readonly Delegates.grpcsharp_batch_context_server_rpc_new_deadline_delegate grpcsharp_batch_context_server_rpc_new_deadline;
        public readonly Delegates.grpcsharp_batch_context_server_rpc_new_request_metadata_delegate grpcsharp_batch_context_server_rpc_new_request_metadata;
        public readonly Delegates.grpcsharp_batch_context_recv_close_on_server_cancelled_delegate grpcsharp_batch_context_recv_close_on_server_cancelled;
        public readonly Delegates.grpcsharp_batch_context_destroy_delegate grpcsharp_batch_context_destroy;

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

        public readonly Delegates.grpcsharp_override_default_ssl_roots grpcsharp_override_default_ssl_roots;
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

        public readonly Delegates.grpcsharp_completion_queue_create_delegate grpcsharp_completion_queue_create;
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
        public readonly Delegates.grpcsharp_metadata_array_get_value_length_delegate grpcsharp_metadata_array_get_value_length;
        public readonly Delegates.grpcsharp_metadata_array_destroy_full_delegate grpcsharp_metadata_array_destroy_full;

        public readonly Delegates.grpcsharp_redirect_log_delegate grpcsharp_redirect_log;

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

        public readonly Delegates.gprsharp_now_delegate gprsharp_now;
        public readonly Delegates.gprsharp_inf_future_delegate gprsharp_inf_future;
        public readonly Delegates.gprsharp_inf_past_delegate gprsharp_inf_past;
        public readonly Delegates.gprsharp_convert_clock_type_delegate gprsharp_convert_clock_type;
        public readonly Delegates.gprsharp_sizeof_timespec_delegate gprsharp_sizeof_timespec;

        public readonly Delegates.grpcsharp_test_callback_delegate grpcsharp_test_callback;
        public readonly Delegates.grpcsharp_test_nop_delegate grpcsharp_test_nop;

        #endregion

        public NativeMethods(UnmanagedLibrary library)
        {
            if (PlatformApis.IsLinux || PlatformApis.IsMacOSX)
            {
                this.grpcsharp_init = GetMethodDelegate<Delegates.grpcsharp_init_delegate>(library);
                this.grpcsharp_shutdown = GetMethodDelegate<Delegates.grpcsharp_shutdown_delegate>(library);
                this.grpcsharp_version_string = GetMethodDelegate<Delegates.grpcsharp_version_string_delegate>(library);

                this.grpcsharp_batch_context_create = GetMethodDelegate<Delegates.grpcsharp_batch_context_create_delegate>(library);
                this.grpcsharp_batch_context_recv_initial_metadata = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_initial_metadata_delegate>(library);
                this.grpcsharp_batch_context_recv_message_length = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_message_length_delegate>(library);
                this.grpcsharp_batch_context_recv_message_to_buffer = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_message_to_buffer_delegate>(library);
                this.grpcsharp_batch_context_recv_status_on_client_status = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_status_on_client_status_delegate>(library);
                this.grpcsharp_batch_context_recv_status_on_client_details = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_status_on_client_details_delegate>(library);
                this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_status_on_client_trailing_metadata_delegate>(library);
                this.grpcsharp_batch_context_server_rpc_new_call = GetMethodDelegate<Delegates.grpcsharp_batch_context_server_rpc_new_call_delegate>(library);
                this.grpcsharp_batch_context_server_rpc_new_method = GetMethodDelegate<Delegates.grpcsharp_batch_context_server_rpc_new_method_delegate>(library);
                this.grpcsharp_batch_context_server_rpc_new_host = GetMethodDelegate<Delegates.grpcsharp_batch_context_server_rpc_new_host_delegate>(library);
                this.grpcsharp_batch_context_server_rpc_new_deadline = GetMethodDelegate<Delegates.grpcsharp_batch_context_server_rpc_new_deadline_delegate>(library);
                this.grpcsharp_batch_context_server_rpc_new_request_metadata = GetMethodDelegate<Delegates.grpcsharp_batch_context_server_rpc_new_request_metadata_delegate>(library);
                this.grpcsharp_batch_context_recv_close_on_server_cancelled = GetMethodDelegate<Delegates.grpcsharp_batch_context_recv_close_on_server_cancelled_delegate>(library);
                this.grpcsharp_batch_context_destroy = GetMethodDelegate<Delegates.grpcsharp_batch_context_destroy_delegate>(library);

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

                this.grpcsharp_override_default_ssl_roots = GetMethodDelegate<Delegates.grpcsharp_override_default_ssl_roots>(library);
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

                this.grpcsharp_completion_queue_create = GetMethodDelegate<Delegates.grpcsharp_completion_queue_create_delegate>(library);
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
                this.grpcsharp_metadata_array_get_value_length = GetMethodDelegate<Delegates.grpcsharp_metadata_array_get_value_length_delegate>(library);
                this.grpcsharp_metadata_array_destroy_full = GetMethodDelegate<Delegates.grpcsharp_metadata_array_destroy_full_delegate>(library);

                this.grpcsharp_redirect_log = GetMethodDelegate<Delegates.grpcsharp_redirect_log_delegate>(library);

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

                this.gprsharp_now = GetMethodDelegate<Delegates.gprsharp_now_delegate>(library);
                this.gprsharp_inf_future = GetMethodDelegate<Delegates.gprsharp_inf_future_delegate>(library);
                this.gprsharp_inf_past = GetMethodDelegate<Delegates.gprsharp_inf_past_delegate>(library);
                this.gprsharp_convert_clock_type = GetMethodDelegate<Delegates.gprsharp_convert_clock_type_delegate>(library);
                this.gprsharp_sizeof_timespec = GetMethodDelegate<Delegates.gprsharp_sizeof_timespec_delegate>(library);

                this.grpcsharp_test_callback = GetMethodDelegate<Delegates.grpcsharp_test_callback_delegate>(library);
                this.grpcsharp_test_nop = GetMethodDelegate<Delegates.grpcsharp_test_nop_delegate>(library);
            }
            else
            {
                // Windows or fallback
                this.grpcsharp_init = PInvokeMethods.grpcsharp_init;
                this.grpcsharp_shutdown = PInvokeMethods.grpcsharp_shutdown;
                this.grpcsharp_version_string = PInvokeMethods.grpcsharp_version_string;

                this.grpcsharp_batch_context_create = PInvokeMethods.grpcsharp_batch_context_create;
                this.grpcsharp_batch_context_recv_initial_metadata = PInvokeMethods.grpcsharp_batch_context_recv_initial_metadata;
                this.grpcsharp_batch_context_recv_message_length = PInvokeMethods.grpcsharp_batch_context_recv_message_length;
                this.grpcsharp_batch_context_recv_message_to_buffer = PInvokeMethods.grpcsharp_batch_context_recv_message_to_buffer;
                this.grpcsharp_batch_context_recv_status_on_client_status = PInvokeMethods.grpcsharp_batch_context_recv_status_on_client_status;
                this.grpcsharp_batch_context_recv_status_on_client_details = PInvokeMethods.grpcsharp_batch_context_recv_status_on_client_details;
                this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = PInvokeMethods.grpcsharp_batch_context_recv_status_on_client_trailing_metadata;
                this.grpcsharp_batch_context_server_rpc_new_call = PInvokeMethods.grpcsharp_batch_context_server_rpc_new_call;
                this.grpcsharp_batch_context_server_rpc_new_method = PInvokeMethods.grpcsharp_batch_context_server_rpc_new_method;
                this.grpcsharp_batch_context_server_rpc_new_host = PInvokeMethods.grpcsharp_batch_context_server_rpc_new_host;
                this.grpcsharp_batch_context_server_rpc_new_deadline = PInvokeMethods.grpcsharp_batch_context_server_rpc_new_deadline;
                this.grpcsharp_batch_context_server_rpc_new_request_metadata = PInvokeMethods.grpcsharp_batch_context_server_rpc_new_request_metadata;
                this.grpcsharp_batch_context_recv_close_on_server_cancelled = PInvokeMethods.grpcsharp_batch_context_recv_close_on_server_cancelled;
                this.grpcsharp_batch_context_destroy = PInvokeMethods.grpcsharp_batch_context_destroy;

                this.grpcsharp_composite_call_credentials_create = PInvokeMethods.grpcsharp_composite_call_credentials_create;
                this.grpcsharp_call_credentials_release = PInvokeMethods.grpcsharp_call_credentials_release;

                this.grpcsharp_call_cancel = PInvokeMethods.grpcsharp_call_cancel;
                this.grpcsharp_call_cancel_with_status = PInvokeMethods.grpcsharp_call_cancel_with_status;
                this.grpcsharp_call_start_unary = PInvokeMethods.grpcsharp_call_start_unary;
                this.grpcsharp_call_start_client_streaming = PInvokeMethods.grpcsharp_call_start_client_streaming;
                this.grpcsharp_call_start_server_streaming = PInvokeMethods.grpcsharp_call_start_server_streaming;
                this.grpcsharp_call_start_duplex_streaming = PInvokeMethods.grpcsharp_call_start_duplex_streaming;
                this.grpcsharp_call_send_message = PInvokeMethods.grpcsharp_call_send_message;
                this.grpcsharp_call_send_close_from_client = PInvokeMethods.grpcsharp_call_send_close_from_client;
                this.grpcsharp_call_send_status_from_server = PInvokeMethods.grpcsharp_call_send_status_from_server;
                this.grpcsharp_call_recv_message = PInvokeMethods.grpcsharp_call_recv_message;
                this.grpcsharp_call_recv_initial_metadata = PInvokeMethods.grpcsharp_call_recv_initial_metadata;
                this.grpcsharp_call_start_serverside = PInvokeMethods.grpcsharp_call_start_serverside;
                this.grpcsharp_call_send_initial_metadata = PInvokeMethods.grpcsharp_call_send_initial_metadata;
                this.grpcsharp_call_set_credentials = PInvokeMethods.grpcsharp_call_set_credentials;
                this.grpcsharp_call_get_peer = PInvokeMethods.grpcsharp_call_get_peer;
                this.grpcsharp_call_destroy = PInvokeMethods.grpcsharp_call_destroy;

                this.grpcsharp_channel_args_create = PInvokeMethods.grpcsharp_channel_args_create;
                this.grpcsharp_channel_args_set_string = PInvokeMethods.grpcsharp_channel_args_set_string;
                this.grpcsharp_channel_args_set_integer = PInvokeMethods.grpcsharp_channel_args_set_integer;
                this.grpcsharp_channel_args_destroy = PInvokeMethods.grpcsharp_channel_args_destroy;

                this.grpcsharp_override_default_ssl_roots = PInvokeMethods.grpcsharp_override_default_ssl_roots;
                this.grpcsharp_ssl_credentials_create = PInvokeMethods.grpcsharp_ssl_credentials_create;
                this.grpcsharp_composite_channel_credentials_create = PInvokeMethods.grpcsharp_composite_channel_credentials_create;
                this.grpcsharp_channel_credentials_release = PInvokeMethods.grpcsharp_channel_credentials_release;

                this.grpcsharp_insecure_channel_create = PInvokeMethods.grpcsharp_insecure_channel_create;
                this.grpcsharp_secure_channel_create = PInvokeMethods.grpcsharp_secure_channel_create;
                this.grpcsharp_channel_create_call = PInvokeMethods.grpcsharp_channel_create_call;
                this.grpcsharp_channel_check_connectivity_state = PInvokeMethods.grpcsharp_channel_check_connectivity_state;
                this.grpcsharp_channel_watch_connectivity_state = PInvokeMethods.grpcsharp_channel_watch_connectivity_state;
                this.grpcsharp_channel_get_target = PInvokeMethods.grpcsharp_channel_get_target;
                this.grpcsharp_channel_destroy = PInvokeMethods.grpcsharp_channel_destroy;

                this.grpcsharp_sizeof_grpc_event = PInvokeMethods.grpcsharp_sizeof_grpc_event;

                this.grpcsharp_completion_queue_create = PInvokeMethods.grpcsharp_completion_queue_create;
                this.grpcsharp_completion_queue_shutdown = PInvokeMethods.grpcsharp_completion_queue_shutdown;
                this.grpcsharp_completion_queue_next = PInvokeMethods.grpcsharp_completion_queue_next;
                this.grpcsharp_completion_queue_pluck = PInvokeMethods.grpcsharp_completion_queue_pluck;
                this.grpcsharp_completion_queue_destroy = PInvokeMethods.grpcsharp_completion_queue_destroy;

                this.gprsharp_free = PInvokeMethods.gprsharp_free;

                this.grpcsharp_metadata_array_create = PInvokeMethods.grpcsharp_metadata_array_create;
                this.grpcsharp_metadata_array_add = PInvokeMethods.grpcsharp_metadata_array_add;
                this.grpcsharp_metadata_array_count = PInvokeMethods.grpcsharp_metadata_array_count;
                this.grpcsharp_metadata_array_get_key = PInvokeMethods.grpcsharp_metadata_array_get_key;
                this.grpcsharp_metadata_array_get_value = PInvokeMethods.grpcsharp_metadata_array_get_value;
                this.grpcsharp_metadata_array_get_value_length = PInvokeMethods.grpcsharp_metadata_array_get_value_length;
                this.grpcsharp_metadata_array_destroy_full = PInvokeMethods.grpcsharp_metadata_array_destroy_full;

                this.grpcsharp_redirect_log = PInvokeMethods.grpcsharp_redirect_log;

                this.grpcsharp_metadata_credentials_create_from_plugin = PInvokeMethods.grpcsharp_metadata_credentials_create_from_plugin;
                this.grpcsharp_metadata_credentials_notify_from_plugin = PInvokeMethods.grpcsharp_metadata_credentials_notify_from_plugin;

                this.grpcsharp_ssl_server_credentials_create = PInvokeMethods.grpcsharp_ssl_server_credentials_create;
                this.grpcsharp_server_credentials_release = PInvokeMethods.grpcsharp_server_credentials_release;

                this.grpcsharp_server_create = PInvokeMethods.grpcsharp_server_create;
                this.grpcsharp_server_register_completion_queue = PInvokeMethods.grpcsharp_server_register_completion_queue;
                this.grpcsharp_server_add_insecure_http2_port = PInvokeMethods.grpcsharp_server_add_insecure_http2_port;
                this.grpcsharp_server_add_secure_http2_port = PInvokeMethods.grpcsharp_server_add_secure_http2_port;
                this.grpcsharp_server_start = PInvokeMethods.grpcsharp_server_start;
                this.grpcsharp_server_request_call = PInvokeMethods.grpcsharp_server_request_call;
                this.grpcsharp_server_cancel_all_calls = PInvokeMethods.grpcsharp_server_cancel_all_calls;
                this.grpcsharp_server_shutdown_and_notify_callback = PInvokeMethods.grpcsharp_server_shutdown_and_notify_callback;
                this.grpcsharp_server_destroy = PInvokeMethods.grpcsharp_server_destroy;

                this.gprsharp_now = PInvokeMethods.gprsharp_now;
                this.gprsharp_inf_future = PInvokeMethods.gprsharp_inf_future;
                this.gprsharp_inf_past = PInvokeMethods.gprsharp_inf_past;
                this.gprsharp_convert_clock_type = PInvokeMethods.gprsharp_convert_clock_type;
                this.gprsharp_sizeof_timespec = PInvokeMethods.gprsharp_sizeof_timespec;

                this.grpcsharp_test_callback = PInvokeMethods.grpcsharp_test_callback;
                this.grpcsharp_test_nop = PInvokeMethods.grpcsharp_test_nop;
            }
        }

        /// <summary>
        /// Gets singleton instance of this class.
        /// </summary>
        public static NativeMethods Get()
        {
            return NativeExtension.Get().NativeMethods;
        }

        static T GetMethodDelegate<T>(UnmanagedLibrary library)
            where T : class
        {
            var methodName = RemoveStringSuffix(typeof(T).Name, "_delegate");
            return library.GetNativeMethodDelegate<T>(methodName);
        }

        static string RemoveStringSuffix(string str, string toRemove)
        {
            if (!str.EndsWith(toRemove))
            {
                return str;
            }
            return str.Substring(0, str.Length - toRemove.Length);
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
            public delegate void grpcsharp_batch_context_recv_message_to_buffer_delegate(BatchContextSafeHandle ctx, byte[] buffer, UIntPtr bufferLen);
            public delegate StatusCode grpcsharp_batch_context_recv_status_on_client_status_delegate(BatchContextSafeHandle ctx);
            public delegate IntPtr grpcsharp_batch_context_recv_status_on_client_details_delegate(BatchContextSafeHandle ctx);  // returns const char*
            public delegate IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata_delegate(BatchContextSafeHandle ctx);
            public delegate CallSafeHandle grpcsharp_batch_context_server_rpc_new_call_delegate(BatchContextSafeHandle ctx);
            public delegate IntPtr grpcsharp_batch_context_server_rpc_new_method_delegate(BatchContextSafeHandle ctx);  // returns const char*
            public delegate IntPtr grpcsharp_batch_context_server_rpc_new_host_delegate(BatchContextSafeHandle ctx);  // returns const char*
            public delegate Timespec grpcsharp_batch_context_server_rpc_new_deadline_delegate(BatchContextSafeHandle ctx);
            public delegate IntPtr grpcsharp_batch_context_server_rpc_new_request_metadata_delegate(BatchContextSafeHandle ctx);
            public delegate int grpcsharp_batch_context_recv_close_on_server_cancelled_delegate(BatchContextSafeHandle ctx);
            public delegate void grpcsharp_batch_context_destroy_delegate(IntPtr ctx);

            public delegate CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create_delegate(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);
            public delegate void grpcsharp_call_credentials_release_delegate(IntPtr credentials);

            public delegate CallError grpcsharp_call_cancel_delegate(CallSafeHandle call);
            public delegate CallError grpcsharp_call_cancel_with_status_delegate(CallSafeHandle call, StatusCode status, string description);
            public delegate CallError grpcsharp_call_start_unary_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, MetadataArraySafeHandle metadataArray, WriteFlags writeFlags);
            public delegate CallError grpcsharp_call_start_client_streaming_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            public delegate CallError grpcsharp_call_start_server_streaming_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen,
                MetadataArraySafeHandle metadataArray, WriteFlags writeFlags);
            public delegate CallError grpcsharp_call_start_duplex_streaming_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            public delegate CallError grpcsharp_call_send_message_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, WriteFlags writeFlags, bool sendEmptyInitialMetadata);
            public delegate CallError grpcsharp_call_send_close_from_client_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx);
            public delegate CallError grpcsharp_call_send_status_from_server_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, StatusCode statusCode, string statusMessage, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata,
                byte[] optionalSendBuffer, UIntPtr optionalSendBufferLen, WriteFlags writeFlags);
            public delegate CallError grpcsharp_call_recv_message_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx);
            public delegate CallError grpcsharp_call_recv_initial_metadata_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx);
            public delegate CallError grpcsharp_call_start_serverside_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx);
            public delegate CallError grpcsharp_call_send_initial_metadata_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
            public delegate CallError grpcsharp_call_set_credentials_delegate(CallSafeHandle call, CallCredentialsSafeHandle credentials);
            public delegate CStringSafeHandle grpcsharp_call_get_peer_delegate(CallSafeHandle call);
            public delegate void grpcsharp_call_destroy_delegate(IntPtr call);

            public delegate ChannelArgsSafeHandle grpcsharp_channel_args_create_delegate(UIntPtr numArgs);
            public delegate void grpcsharp_channel_args_set_string_delegate(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);
            public delegate void grpcsharp_channel_args_set_integer_delegate(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);
            public delegate void grpcsharp_channel_args_destroy_delegate(IntPtr args);

            public delegate void grpcsharp_override_default_ssl_roots(string pemRootCerts);
            public delegate ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create_delegate(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey);
            public delegate ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create_delegate(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);
            public delegate void grpcsharp_channel_credentials_release_delegate(IntPtr credentials);

            public delegate ChannelSafeHandle grpcsharp_insecure_channel_create_delegate(string target, ChannelArgsSafeHandle channelArgs);
            public delegate ChannelSafeHandle grpcsharp_secure_channel_create_delegate(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);
            public delegate CallSafeHandle grpcsharp_channel_create_call_delegate(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);
            public delegate ChannelState grpcsharp_channel_check_connectivity_state_delegate(ChannelSafeHandle channel, int tryToConnect);
            public delegate void grpcsharp_channel_watch_connectivity_state_delegate(ChannelSafeHandle channel, ChannelState lastObservedState,
                Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            public delegate CStringSafeHandle grpcsharp_channel_get_target_delegate(ChannelSafeHandle call);
            public delegate void grpcsharp_channel_destroy_delegate(IntPtr channel);

            public delegate int grpcsharp_sizeof_grpc_event_delegate();

            public delegate CompletionQueueSafeHandle grpcsharp_completion_queue_create_delegate();
            public delegate void grpcsharp_completion_queue_shutdown_delegate(CompletionQueueSafeHandle cq);
            public delegate CompletionQueueEvent grpcsharp_completion_queue_next_delegate(CompletionQueueSafeHandle cq);
            public delegate CompletionQueueEvent grpcsharp_completion_queue_pluck_delegate(CompletionQueueSafeHandle cq, IntPtr tag);
            public delegate void grpcsharp_completion_queue_destroy_delegate(IntPtr cq);

            public delegate void gprsharp_free_delegate(IntPtr ptr);

            public delegate MetadataArraySafeHandle grpcsharp_metadata_array_create_delegate(UIntPtr capacity);
            public delegate void grpcsharp_metadata_array_add_delegate(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);
            public delegate UIntPtr grpcsharp_metadata_array_count_delegate(IntPtr metadataArray);
            public delegate IntPtr grpcsharp_metadata_array_get_key_delegate(IntPtr metadataArray, UIntPtr index);
            public delegate IntPtr grpcsharp_metadata_array_get_value_delegate(IntPtr metadataArray, UIntPtr index);
            public delegate UIntPtr grpcsharp_metadata_array_get_value_length_delegate(IntPtr metadataArray, UIntPtr index);
            public delegate void grpcsharp_metadata_array_destroy_full_delegate(IntPtr array);

            public delegate void grpcsharp_redirect_log_delegate(GprLogDelegate callback);

            public delegate CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin_delegate(NativeMetadataInterceptor interceptor);
            public delegate void grpcsharp_metadata_credentials_notify_from_plugin_delegate(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);

            public delegate ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create_delegate(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, bool forceClientAuth);
            public delegate void grpcsharp_server_credentials_release_delegate(IntPtr credentials);

            public delegate ServerSafeHandle grpcsharp_server_create_delegate(ChannelArgsSafeHandle args);
            public delegate void grpcsharp_server_register_completion_queue_delegate(ServerSafeHandle server, CompletionQueueSafeHandle cq);
            public delegate int grpcsharp_server_add_insecure_http2_port_delegate(ServerSafeHandle server, string addr);
            public delegate int grpcsharp_server_add_secure_http2_port_delegate(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);
            public delegate void grpcsharp_server_start_delegate(ServerSafeHandle server);
            public delegate CallError grpcsharp_server_request_call_delegate(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            public delegate void grpcsharp_server_cancel_all_calls_delegate(ServerSafeHandle server);
            public delegate void grpcsharp_server_shutdown_and_notify_callback_delegate(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);
            public delegate void grpcsharp_server_destroy_delegate(IntPtr server);

            public delegate Timespec gprsharp_now_delegate(ClockType clockType);
            public delegate Timespec gprsharp_inf_future_delegate(ClockType clockType);
            public delegate Timespec gprsharp_inf_past_delegate(ClockType clockType);

            public delegate Timespec gprsharp_convert_clock_type_delegate(Timespec t, ClockType targetClock);
            public delegate int gprsharp_sizeof_timespec_delegate();

            public delegate CallError grpcsharp_test_callback_delegate([MarshalAs(UnmanagedType.FunctionPtr)] OpCompletionDelegate callback);
            public delegate IntPtr grpcsharp_test_nop_delegate(IntPtr ptr);
        }

        /// <summary>
        /// Default PInvoke bindings for native methods that are used on Windows.
        /// Alternatively, they can also be used as a fallback on Mono
        /// (if libgrpc_csharp_ext is installed on your system, or is made accessible through e.g. LD_LIBRARY_PATH environment variable
        /// or using Mono's dllMap feature).
        /// </summary>
        private class PInvokeMethods
        {
            // Environment

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_init();

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_shutdown();

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_version_string();  // returns not-owned const char*

            // BatchContextSafeHandle

            [DllImport("grpc_csharp_ext.dll")]
            public static extern BatchContextSafeHandle grpcsharp_batch_context_create();

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_batch_context_recv_message_to_buffer(BatchContextSafeHandle ctx, byte[] buffer, UIntPtr bufferLen);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx);  // returns const char*

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallSafeHandle grpcsharp_batch_context_server_rpc_new_call(BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_batch_context_server_rpc_new_method(BatchContextSafeHandle ctx);  // returns const char*

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_batch_context_server_rpc_new_host(BatchContextSafeHandle ctx);  // returns const char*

            [DllImport("grpc_csharp_ext.dll")]
            public static extern Timespec grpcsharp_batch_context_server_rpc_new_deadline(BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_batch_context_server_rpc_new_request_metadata(BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_batch_context_destroy(IntPtr ctx);

            // CallCredentialsSafeHandle

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_call_credentials_release(IntPtr credentials);

            // CallSafeHandle

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_cancel(CallSafeHandle call);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_start_unary(CallSafeHandle call,
                BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, MetadataArraySafeHandle metadataArray, WriteFlags writeFlags);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_start_client_streaming(CallSafeHandle call,
                BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_start_server_streaming(CallSafeHandle call,
                BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen,
                MetadataArraySafeHandle metadataArray, WriteFlags writeFlags);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call,
                BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_send_message(CallSafeHandle call,
                BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, WriteFlags writeFlags, bool sendEmptyInitialMetadata);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_send_close_from_client(CallSafeHandle call,
                BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_send_status_from_server(CallSafeHandle call,
                BatchContextSafeHandle ctx, StatusCode statusCode, string statusMessage, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata,
                byte[] optionalSendBuffer, UIntPtr optionalSendBufferLen, WriteFlags writeFlags);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_recv_message(CallSafeHandle call,
                BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call,
                BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_start_serverside(CallSafeHandle call,
                BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_send_initial_metadata(CallSafeHandle call,
                BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_call_destroy(IntPtr call);

            // ChannelArgsSafeHandle 

            [DllImport("grpc_csharp_ext.dll")]
            public static extern ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);

            [DllImport("grpc_csharp_ext.dll", CharSet = CharSet.Ansi)]
            public static extern void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);

            [DllImport("grpc_csharp_ext.dll", CharSet = CharSet.Ansi)]
            public static extern void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_channel_args_destroy(IntPtr args);

            // ChannelCredentialsSafeHandle

            [DllImport("grpc_csharp_ext.dll", CharSet = CharSet.Ansi)]
            public static extern void grpcsharp_override_default_ssl_roots(string pemRootCerts);

            [DllImport("grpc_csharp_ext.dll", CharSet = CharSet.Ansi)]
            public static extern ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_channel_credentials_release(IntPtr credentials);

            // ChannelSafeHandle

            [DllImport("grpc_csharp_ext.dll")]
            public static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState,
                Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_channel_destroy(IntPtr channel);

            // CompletionQueueEvent

            [DllImport("grpc_csharp_ext.dll")]
            public static extern int grpcsharp_sizeof_grpc_event();

            // CompletionQueueSafeHandle

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create();

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_completion_queue_destroy(IntPtr cq);

            // CStringSafeHandle 

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void gprsharp_free(IntPtr ptr);

            // MetadataArraySafeHandle

            [DllImport("grpc_csharp_ext.dll")]
            public static extern MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);

            [DllImport("grpc_csharp_ext.dll", CharSet = CharSet.Ansi)]
            public static extern void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern UIntPtr grpcsharp_metadata_array_get_value_length(IntPtr metadataArray, UIntPtr index);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_metadata_array_destroy_full(IntPtr array);

            // NativeLogRedirector

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_redirect_log(GprLogDelegate callback);

            // NativeMetadataCredentialsPlugin

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(NativeMetadataInterceptor interceptor);

            [DllImport("grpc_csharp_ext.dll", CharSet = CharSet.Ansi)]
            public static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);

            // ServerCredentialsSafeHandle

            [DllImport("grpc_csharp_ext.dll", CharSet = CharSet.Ansi)]
            public static extern ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, bool forceClientAuth);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_server_credentials_release(IntPtr credentials);

            // ServerSafeHandle

            [DllImport("grpc_csharp_ext.dll")]
            public static extern ServerSafeHandle grpcsharp_server_create(ChannelArgsSafeHandle args);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_server_register_completion_queue(ServerSafeHandle server, CompletionQueueSafeHandle cq);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_server_start(ServerSafeHandle server);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern void grpcsharp_server_destroy(IntPtr server);

            // Timespec

            [DllImport("grpc_csharp_ext.dll")]
            public static extern Timespec gprsharp_now(ClockType clockType);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern Timespec gprsharp_inf_future(ClockType clockType);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern Timespec gprsharp_inf_past(ClockType clockType);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern Timespec gprsharp_convert_clock_type(Timespec t, ClockType targetClock);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern int gprsharp_sizeof_timespec();

            // Testing

            [DllImport("grpc_csharp_ext.dll")]
            public static extern CallError grpcsharp_test_callback([MarshalAs(UnmanagedType.FunctionPtr)] OpCompletionDelegate callback);

            [DllImport("grpc_csharp_ext.dll")]
            public static extern IntPtr grpcsharp_test_nop(IntPtr ptr);
        }
    }
}
