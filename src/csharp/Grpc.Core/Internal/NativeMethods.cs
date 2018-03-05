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
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

using Grpc.Core.Logging;
using Grpc.Core.Utils;
using Omni.Logging;
using Omni.Utils;

namespace Grpc.Core.Internal
{
    internal delegate void NativeCallbackTestDelegate(bool success);

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
        public readonly Delegates.grpcsharp_batch_context_recv_close_on_server_cancelled_delegate grpcsharp_batch_context_recv_close_on_server_cancelled;
        public readonly Delegates.grpcsharp_batch_context_reset_delegate grpcsharp_batch_context_reset;
        public readonly Delegates.grpcsharp_batch_context_destroy_delegate grpcsharp_batch_context_destroy;

        public readonly Delegates.grpcsharp_request_call_context_create_delegate grpcsharp_request_call_context_create;
        public readonly Delegates.grpcsharp_request_call_context_call_delegate grpcsharp_request_call_context_call;
        public readonly Delegates.grpcsharp_request_call_context_method_delegate grpcsharp_request_call_context_method;
        public readonly Delegates.grpcsharp_request_call_context_host_delegate grpcsharp_request_call_context_host;
        public readonly Delegates.grpcsharp_request_call_context_deadline_delegate grpcsharp_request_call_context_deadline;
        public readonly Delegates.grpcsharp_request_call_context_request_metadata_delegate grpcsharp_request_call_context_request_metadata;
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

        public readonly Delegates.gprsharp_now_delegate gprsharp_now;
        public readonly Delegates.gprsharp_inf_future_delegate gprsharp_inf_future;
        public readonly Delegates.gprsharp_inf_past_delegate gprsharp_inf_past;
        public readonly Delegates.gprsharp_convert_clock_type_delegate gprsharp_convert_clock_type;
        public readonly Delegates.gprsharp_sizeof_timespec_delegate gprsharp_sizeof_timespec;

        public readonly Delegates.grpcsharp_test_callback_delegate grpcsharp_test_callback;
        public readonly Delegates.grpcsharp_test_nop_delegate grpcsharp_test_nop;

        public readonly Delegates.grpcsharp_test_override_method_delegate grpcsharp_test_override_method;

        #endregion

        public NativeMethods(UnmanagedLibrary library) {
            this.grpcsharp_init = GetMethod<Delegates.grpcsharp_init_delegate>(
              LibBridgeExt.grpcsharp_init,
              LibBridgeInternal.grpcsharp_init,
              library
            );
            this.grpcsharp_shutdown = GetMethod<Delegates.grpcsharp_shutdown_delegate>(
              LibBridgeExt.grpcsharp_shutdown,
              LibBridgeInternal.grpcsharp_shutdown,
              library
            );
            this.grpcsharp_version_string = GetMethod<Delegates.grpcsharp_version_string_delegate>(
              LibBridgeExt.grpcsharp_version_string,
              LibBridgeInternal.grpcsharp_version_string,
              library
            );

            this.grpcsharp_batch_context_create = GetMethod<Delegates.grpcsharp_batch_context_create_delegate>(
              LibBridgeExt.grpcsharp_batch_context_create,
              LibBridgeInternal.grpcsharp_batch_context_create,
              library
            );
            this.grpcsharp_batch_context_recv_initial_metadata = GetMethod<Delegates.grpcsharp_batch_context_recv_initial_metadata_delegate>(
              LibBridgeExt.grpcsharp_batch_context_recv_initial_metadata,
              LibBridgeInternal.grpcsharp_batch_context_recv_initial_metadata,
              library
            );
            this.grpcsharp_batch_context_recv_message_length = GetMethod<Delegates.grpcsharp_batch_context_recv_message_length_delegate>(
              LibBridgeExt.grpcsharp_batch_context_recv_message_length,
              LibBridgeInternal.grpcsharp_batch_context_recv_message_length,
              library
            );
            this.grpcsharp_batch_context_recv_message_to_buffer = GetMethod<Delegates.grpcsharp_batch_context_recv_message_to_buffer_delegate>(
              LibBridgeExt.grpcsharp_batch_context_recv_message_to_buffer,
              LibBridgeInternal.grpcsharp_batch_context_recv_message_to_buffer,
              library
            );
            this.grpcsharp_batch_context_recv_status_on_client_status = GetMethod<Delegates.grpcsharp_batch_context_recv_status_on_client_status_delegate>(
              LibBridgeExt.grpcsharp_batch_context_recv_status_on_client_status,
              LibBridgeInternal.grpcsharp_batch_context_recv_status_on_client_status,
              library
            );
            this.grpcsharp_batch_context_recv_status_on_client_details = GetMethod<Delegates.grpcsharp_batch_context_recv_status_on_client_details_delegate>(
              LibBridgeExt.grpcsharp_batch_context_recv_status_on_client_details,
              LibBridgeInternal.grpcsharp_batch_context_recv_status_on_client_details,
              library
            );
            this.grpcsharp_batch_context_recv_status_on_client_trailing_metadata = GetMethod<Delegates.grpcsharp_batch_context_recv_status_on_client_trailing_metadata_delegate>(
              LibBridgeExt.grpcsharp_batch_context_recv_status_on_client_trailing_metadata,
              LibBridgeInternal.grpcsharp_batch_context_recv_status_on_client_trailing_metadata,
              library
            );
            this.grpcsharp_batch_context_recv_close_on_server_cancelled = GetMethod<Delegates.grpcsharp_batch_context_recv_close_on_server_cancelled_delegate>(
              LibBridgeExt.grpcsharp_batch_context_recv_close_on_server_cancelled,
              LibBridgeInternal.grpcsharp_batch_context_recv_close_on_server_cancelled,
              library
            );
            this.grpcsharp_batch_context_reset = GetMethod<Delegates.grpcsharp_batch_context_reset_delegate>(
              LibBridgeExt.grpcsharp_batch_context_reset,
              LibBridgeInternal.grpcsharp_batch_context_reset,
              library
            );
            this.grpcsharp_batch_context_destroy = GetMethod<Delegates.grpcsharp_batch_context_destroy_delegate>(
              LibBridgeExt.grpcsharp_batch_context_destroy,
              LibBridgeInternal.grpcsharp_batch_context_destroy,
              library
            );

            this.grpcsharp_request_call_context_create = GetMethod<Delegates.grpcsharp_request_call_context_create_delegate>(
              LibBridgeExt.grpcsharp_request_call_context_create,
              LibBridgeInternal.grpcsharp_request_call_context_create,
              library
            );
            this.grpcsharp_request_call_context_call = GetMethod<Delegates.grpcsharp_request_call_context_call_delegate>(
              LibBridgeExt.grpcsharp_request_call_context_call,
              LibBridgeInternal.grpcsharp_request_call_context_call,
              library
            );
            this.grpcsharp_request_call_context_method = GetMethod<Delegates.grpcsharp_request_call_context_method_delegate>(
              LibBridgeExt.grpcsharp_request_call_context_method,
              LibBridgeInternal.grpcsharp_request_call_context_method,
              library
            );
            this.grpcsharp_request_call_context_host = GetMethod<Delegates.grpcsharp_request_call_context_host_delegate>(
              LibBridgeExt.grpcsharp_request_call_context_host,
              LibBridgeInternal.grpcsharp_request_call_context_host,
              library
            );
            this.grpcsharp_request_call_context_deadline = GetMethod<Delegates.grpcsharp_request_call_context_deadline_delegate>(
              LibBridgeExt.grpcsharp_request_call_context_deadline,
              LibBridgeInternal.grpcsharp_request_call_context_deadline,
              library
            );
            this.grpcsharp_request_call_context_request_metadata = GetMethod<Delegates.grpcsharp_request_call_context_request_metadata_delegate>(
              LibBridgeExt.grpcsharp_request_call_context_request_metadata,
              LibBridgeInternal.grpcsharp_request_call_context_request_metadata,
              library
            );
            this.grpcsharp_request_call_context_destroy = GetMethod<Delegates.grpcsharp_request_call_context_destroy_delegate>(
              LibBridgeExt.grpcsharp_request_call_context_destroy,
              LibBridgeInternal.grpcsharp_request_call_context_destroy,
              library
            );

            this.grpcsharp_composite_call_credentials_create = GetMethod<Delegates.grpcsharp_composite_call_credentials_create_delegate>(
              LibBridgeExt.grpcsharp_composite_call_credentials_create,
              LibBridgeInternal.grpcsharp_composite_call_credentials_create,
              library
            );
            this.grpcsharp_call_credentials_release = GetMethod<Delegates.grpcsharp_call_credentials_release_delegate>(
              LibBridgeExt.grpcsharp_call_credentials_release,
              LibBridgeInternal.grpcsharp_call_credentials_release,
              library
            );

            this.grpcsharp_call_cancel = GetMethod<Delegates.grpcsharp_call_cancel_delegate>(
              LibBridgeExt.grpcsharp_call_cancel,
              LibBridgeInternal.grpcsharp_call_cancel,
              library
            );
            this.grpcsharp_call_cancel_with_status = GetMethod<Delegates.grpcsharp_call_cancel_with_status_delegate>(
              LibBridgeExt.grpcsharp_call_cancel_with_status,
              LibBridgeInternal.grpcsharp_call_cancel_with_status,
              library
            );
            this.grpcsharp_call_start_unary = GetMethod<Delegates.grpcsharp_call_start_unary_delegate>(
              LibBridgeExt.grpcsharp_call_start_unary,
              LibBridgeInternal.grpcsharp_call_start_unary,
              library
            );
            this.grpcsharp_call_start_client_streaming = GetMethod<Delegates.grpcsharp_call_start_client_streaming_delegate>(
              LibBridgeExt.grpcsharp_call_start_client_streaming,
              LibBridgeInternal.grpcsharp_call_start_client_streaming,
              library
            );
            this.grpcsharp_call_start_server_streaming = GetMethod<Delegates.grpcsharp_call_start_server_streaming_delegate>(
              LibBridgeExt.grpcsharp_call_start_server_streaming,
              LibBridgeInternal.grpcsharp_call_start_server_streaming,
              library
            );
            this.grpcsharp_call_start_duplex_streaming = GetMethod<Delegates.grpcsharp_call_start_duplex_streaming_delegate>(
              LibBridgeExt.grpcsharp_call_start_duplex_streaming,
              LibBridgeInternal.grpcsharp_call_start_duplex_streaming,
              library
            );
            this.grpcsharp_call_send_message = GetMethod<Delegates.grpcsharp_call_send_message_delegate>(
              LibBridgeExt.grpcsharp_call_send_message,
              LibBridgeInternal.grpcsharp_call_send_message,
              library
            );
            this.grpcsharp_call_send_close_from_client = GetMethod<Delegates.grpcsharp_call_send_close_from_client_delegate>(
              LibBridgeExt.grpcsharp_call_send_close_from_client,
              LibBridgeInternal.grpcsharp_call_send_close_from_client,
              library
            );
            this.grpcsharp_call_send_status_from_server = GetMethod<Delegates.grpcsharp_call_send_status_from_server_delegate>(
              LibBridgeExt.grpcsharp_call_send_status_from_server,
              LibBridgeInternal.grpcsharp_call_send_status_from_server,
              library
            );
            this.grpcsharp_call_recv_message = GetMethod<Delegates.grpcsharp_call_recv_message_delegate>(
              LibBridgeExt.grpcsharp_call_recv_message,
              LibBridgeInternal.grpcsharp_call_recv_message,
              library
            );
            this.grpcsharp_call_recv_initial_metadata = GetMethod<Delegates.grpcsharp_call_recv_initial_metadata_delegate>(
              LibBridgeExt.grpcsharp_call_recv_initial_metadata,
              LibBridgeInternal.grpcsharp_call_recv_initial_metadata,
              library
            );
            this.grpcsharp_call_start_serverside = GetMethod<Delegates.grpcsharp_call_start_serverside_delegate>(
              LibBridgeExt.grpcsharp_call_start_serverside,
              LibBridgeInternal.grpcsharp_call_start_serverside,
              library
            );
            this.grpcsharp_call_send_initial_metadata = GetMethod<Delegates.grpcsharp_call_send_initial_metadata_delegate>(
              LibBridgeExt.grpcsharp_call_send_initial_metadata,
              LibBridgeInternal.grpcsharp_call_send_initial_metadata,
              library
            );
            this.grpcsharp_call_set_credentials = GetMethod<Delegates.grpcsharp_call_set_credentials_delegate>(
              LibBridgeExt.grpcsharp_call_set_credentials,
              LibBridgeInternal.grpcsharp_call_set_credentials,
              library
            );
            this.grpcsharp_call_get_peer = GetMethod<Delegates.grpcsharp_call_get_peer_delegate>(
              LibBridgeExt.grpcsharp_call_get_peer,
              LibBridgeInternal.grpcsharp_call_get_peer,
              library
            );
            this.grpcsharp_call_destroy = GetMethod<Delegates.grpcsharp_call_destroy_delegate>(
              LibBridgeExt.grpcsharp_call_destroy,
              LibBridgeInternal.grpcsharp_call_destroy,
              library
            );

            this.grpcsharp_channel_args_create = GetMethod<Delegates.grpcsharp_channel_args_create_delegate>(
              LibBridgeExt.grpcsharp_channel_args_create,
              LibBridgeInternal.grpcsharp_channel_args_create,
              library
            );
            this.grpcsharp_channel_args_set_string = GetMethod<Delegates.grpcsharp_channel_args_set_string_delegate>(
              LibBridgeExt.grpcsharp_channel_args_set_string,
              LibBridgeInternal.grpcsharp_channel_args_set_string,
              library
            );
            this.grpcsharp_channel_args_set_integer = GetMethod<Delegates.grpcsharp_channel_args_set_integer_delegate>(
              LibBridgeExt.grpcsharp_channel_args_set_integer,
              LibBridgeInternal.grpcsharp_channel_args_set_integer,
              library
            );
            this.grpcsharp_channel_args_destroy = GetMethod<Delegates.grpcsharp_channel_args_destroy_delegate>(
              LibBridgeExt.grpcsharp_channel_args_destroy,
              LibBridgeInternal.grpcsharp_channel_args_destroy,
              library
            );

            this.grpcsharp_override_default_ssl_roots = GetMethod<Delegates.grpcsharp_override_default_ssl_roots>(
              LibBridgeExt.grpcsharp_override_default_ssl_roots,
              LibBridgeInternal.grpcsharp_override_default_ssl_roots,
              library
            );
            this.grpcsharp_ssl_credentials_create = GetMethod<Delegates.grpcsharp_ssl_credentials_create_delegate>(
              LibBridgeExt.grpcsharp_ssl_credentials_create,
              LibBridgeInternal.grpcsharp_ssl_credentials_create,
              library
            );
            this.grpcsharp_composite_channel_credentials_create = GetMethod<Delegates.grpcsharp_composite_channel_credentials_create_delegate>(
              LibBridgeExt.grpcsharp_composite_channel_credentials_create,
              LibBridgeInternal.grpcsharp_composite_channel_credentials_create,
              library
            );
            this.grpcsharp_channel_credentials_release = GetMethod<Delegates.grpcsharp_channel_credentials_release_delegate>(
              LibBridgeExt.grpcsharp_channel_credentials_release,
              LibBridgeInternal.grpcsharp_channel_credentials_release,
              library
            );

            this.grpcsharp_insecure_channel_create = GetMethod<Delegates.grpcsharp_insecure_channel_create_delegate>(
              LibBridgeExt.grpcsharp_insecure_channel_create,
              LibBridgeInternal.grpcsharp_insecure_channel_create,
              library
            );
            this.grpcsharp_secure_channel_create = GetMethod<Delegates.grpcsharp_secure_channel_create_delegate>(
              LibBridgeExt.grpcsharp_secure_channel_create,
              LibBridgeInternal.grpcsharp_secure_channel_create,
              library
            );
            this.grpcsharp_channel_create_call = GetMethod<Delegates.grpcsharp_channel_create_call_delegate>(
              LibBridgeExt.grpcsharp_channel_create_call,
              LibBridgeInternal.grpcsharp_channel_create_call,
              library
            );
            this.grpcsharp_channel_check_connectivity_state = GetMethod<Delegates.grpcsharp_channel_check_connectivity_state_delegate>(
              LibBridgeExt.grpcsharp_channel_check_connectivity_state,
              LibBridgeInternal.grpcsharp_channel_check_connectivity_state,
              library
            );
            this.grpcsharp_channel_watch_connectivity_state = GetMethod<Delegates.grpcsharp_channel_watch_connectivity_state_delegate>(
              LibBridgeExt.grpcsharp_channel_watch_connectivity_state,
              LibBridgeInternal.grpcsharp_channel_watch_connectivity_state,
              library
            );
            this.grpcsharp_channel_get_target = GetMethod<Delegates.grpcsharp_channel_get_target_delegate>(
              LibBridgeExt.grpcsharp_channel_get_target,
              LibBridgeInternal.grpcsharp_channel_get_target,
              library
            );
            this.grpcsharp_channel_destroy = GetMethod<Delegates.grpcsharp_channel_destroy_delegate>(
              LibBridgeExt.grpcsharp_channel_destroy,
              LibBridgeInternal.grpcsharp_channel_destroy,
              library
            );

            this.grpcsharp_sizeof_grpc_event = GetMethod<Delegates.grpcsharp_sizeof_grpc_event_delegate>(
              LibBridgeExt.grpcsharp_sizeof_grpc_event,
              LibBridgeInternal.grpcsharp_sizeof_grpc_event,
              library
            );

            this.grpcsharp_completion_queue_create_async = GetMethod<Delegates.grpcsharp_completion_queue_create_async_delegate>(
              LibBridgeExt.grpcsharp_completion_queue_create_async,
              LibBridgeInternal.grpcsharp_completion_queue_create_async,
              library
            );
            this.grpcsharp_completion_queue_create_sync = GetMethod<Delegates.grpcsharp_completion_queue_create_sync_delegate>(
              LibBridgeExt.grpcsharp_completion_queue_create_sync,
              LibBridgeInternal.grpcsharp_completion_queue_create_sync,
              library
            );
            this.grpcsharp_completion_queue_shutdown = GetMethod<Delegates.grpcsharp_completion_queue_shutdown_delegate>(
              LibBridgeExt.grpcsharp_completion_queue_shutdown,
              LibBridgeInternal.grpcsharp_completion_queue_shutdown,
              library
            );
            this.grpcsharp_completion_queue_next = GetMethod<Delegates.grpcsharp_completion_queue_next_delegate>(
              LibBridgeExt.grpcsharp_completion_queue_next,
              LibBridgeInternal.grpcsharp_completion_queue_next,
              library
            );
            this.grpcsharp_completion_queue_pluck = GetMethod<Delegates.grpcsharp_completion_queue_pluck_delegate>(
              LibBridgeExt.grpcsharp_completion_queue_pluck,
              LibBridgeInternal.grpcsharp_completion_queue_pluck,
              library
            );
            this.grpcsharp_completion_queue_destroy = GetMethod<Delegates.grpcsharp_completion_queue_destroy_delegate>(
              LibBridgeExt.grpcsharp_completion_queue_destroy,
              LibBridgeInternal.grpcsharp_completion_queue_destroy,
              library
            );

            this.gprsharp_free = GetMethod<Delegates.gprsharp_free_delegate>(
              LibBridgeExt.gprsharp_free,
              LibBridgeInternal.gprsharp_free,
              library
            );

            this.grpcsharp_metadata_array_create = GetMethod<Delegates.grpcsharp_metadata_array_create_delegate>(
              LibBridgeExt.grpcsharp_metadata_array_create,
              LibBridgeInternal.grpcsharp_metadata_array_create,
              library
            );
            this.grpcsharp_metadata_array_add = GetMethod<Delegates.grpcsharp_metadata_array_add_delegate>(
              LibBridgeExt.grpcsharp_metadata_array_add,
              LibBridgeInternal.grpcsharp_metadata_array_add,
              library
            );
            this.grpcsharp_metadata_array_count = GetMethod<Delegates.grpcsharp_metadata_array_count_delegate>(
              LibBridgeExt.grpcsharp_metadata_array_count,
              LibBridgeInternal.grpcsharp_metadata_array_count,
              library
            );
            this.grpcsharp_metadata_array_get_key = GetMethod<Delegates.grpcsharp_metadata_array_get_key_delegate>(
              LibBridgeExt.grpcsharp_metadata_array_get_key,
              LibBridgeInternal.grpcsharp_metadata_array_get_key,
              library
            );
            this.grpcsharp_metadata_array_get_value = GetMethod<Delegates.grpcsharp_metadata_array_get_value_delegate>(
              LibBridgeExt.grpcsharp_metadata_array_get_value,
              LibBridgeInternal.grpcsharp_metadata_array_get_value,
              library
            );
            this.grpcsharp_metadata_array_destroy_full = GetMethod<Delegates.grpcsharp_metadata_array_destroy_full_delegate>(
              LibBridgeExt.grpcsharp_metadata_array_destroy_full,
              LibBridgeInternal.grpcsharp_metadata_array_destroy_full,
              library
            );

            this.grpcsharp_redirect_log = GetMethod<Delegates.grpcsharp_redirect_log_delegate>(
              LibBridgeExt.grpcsharp_redirect_log,
              LibBridgeInternal.grpcsharp_redirect_log,
              library
            );

            this.grpcsharp_metadata_credentials_create_from_plugin = GetMethod<Delegates.grpcsharp_metadata_credentials_create_from_plugin_delegate>(
              LibBridgeExt.grpcsharp_metadata_credentials_create_from_plugin,
              LibBridgeInternal.grpcsharp_metadata_credentials_create_from_plugin,
              library
            );
            this.grpcsharp_metadata_credentials_notify_from_plugin = GetMethod<Delegates.grpcsharp_metadata_credentials_notify_from_plugin_delegate>(
              LibBridgeExt.grpcsharp_metadata_credentials_notify_from_plugin,
              LibBridgeInternal.grpcsharp_metadata_credentials_notify_from_plugin,
              library
            );

            this.grpcsharp_ssl_server_credentials_create = GetMethod<Delegates.grpcsharp_ssl_server_credentials_create_delegate>(
              LibBridgeExt.grpcsharp_ssl_server_credentials_create,
              LibBridgeInternal.grpcsharp_ssl_server_credentials_create,
              library
            );
            this.grpcsharp_server_credentials_release = GetMethod<Delegates.grpcsharp_server_credentials_release_delegate>(
              LibBridgeExt.grpcsharp_server_credentials_release,
              LibBridgeInternal.grpcsharp_server_credentials_release,
              library
            );

            this.grpcsharp_server_create = GetMethod<Delegates.grpcsharp_server_create_delegate>(
              LibBridgeExt.grpcsharp_server_create,
              LibBridgeInternal.grpcsharp_server_create,
              library
            );
            this.grpcsharp_server_register_completion_queue = GetMethod<Delegates.grpcsharp_server_register_completion_queue_delegate>(
              LibBridgeExt.grpcsharp_server_register_completion_queue,
              LibBridgeInternal.grpcsharp_server_register_completion_queue,
              library
            );
            this.grpcsharp_server_add_insecure_http2_port = GetMethod<Delegates.grpcsharp_server_add_insecure_http2_port_delegate>(
              LibBridgeExt.grpcsharp_server_add_insecure_http2_port,
              LibBridgeInternal.grpcsharp_server_add_insecure_http2_port,
              library
            );
            this.grpcsharp_server_add_secure_http2_port = GetMethod<Delegates.grpcsharp_server_add_secure_http2_port_delegate>(
              LibBridgeExt.grpcsharp_server_add_secure_http2_port,
              LibBridgeInternal.grpcsharp_server_add_secure_http2_port,
              library
            );
            this.grpcsharp_server_start = GetMethod<Delegates.grpcsharp_server_start_delegate>(
              LibBridgeExt.grpcsharp_server_start,
              LibBridgeInternal.grpcsharp_server_start,
              library
            );
            this.grpcsharp_server_request_call = GetMethod<Delegates.grpcsharp_server_request_call_delegate>(
              LibBridgeExt.grpcsharp_server_request_call,
              LibBridgeInternal.grpcsharp_server_request_call,
              library
            );
            this.grpcsharp_server_cancel_all_calls = GetMethod<Delegates.grpcsharp_server_cancel_all_calls_delegate>(
              LibBridgeExt.grpcsharp_server_cancel_all_calls,
              LibBridgeInternal.grpcsharp_server_cancel_all_calls,
              library
            );
            this.grpcsharp_server_shutdown_and_notify_callback = GetMethod<Delegates.grpcsharp_server_shutdown_and_notify_callback_delegate>(
              LibBridgeExt.grpcsharp_server_shutdown_and_notify_callback,
              LibBridgeInternal.grpcsharp_server_shutdown_and_notify_callback,
              library
            );
            this.grpcsharp_server_destroy = GetMethod<Delegates.grpcsharp_server_destroy_delegate>(
              LibBridgeExt.grpcsharp_server_destroy,
              LibBridgeInternal.grpcsharp_server_destroy,
              library
            );

            this.grpcsharp_call_auth_context = GetMethod<Delegates.grpcsharp_call_auth_context_delegate>(
              LibBridgeExt.grpcsharp_call_auth_context,
              LibBridgeInternal.grpcsharp_call_auth_context,
              library
            );
            this.grpcsharp_auth_context_peer_identity_property_name = GetMethod<Delegates.grpcsharp_auth_context_peer_identity_property_name_delegate>(
              LibBridgeExt.grpcsharp_auth_context_peer_identity_property_name,
              LibBridgeInternal.grpcsharp_auth_context_peer_identity_property_name,
              library
            );
            this.grpcsharp_auth_context_property_iterator = GetMethod<Delegates.grpcsharp_auth_context_property_iterator_delegate>(
              LibBridgeExt.grpcsharp_auth_context_property_iterator,
              LibBridgeInternal.grpcsharp_auth_context_property_iterator,
              library
            );
            this.grpcsharp_auth_property_iterator_next = GetMethod<Delegates.grpcsharp_auth_property_iterator_next_delegate>(
              LibBridgeExt.grpcsharp_auth_property_iterator_next,
              LibBridgeInternal.grpcsharp_auth_property_iterator_next,
              library
            );
            this.grpcsharp_auth_context_release = GetMethod<Delegates.grpcsharp_auth_context_release_delegate>(
              LibBridgeExt.grpcsharp_auth_context_release,
              LibBridgeInternal.grpcsharp_auth_context_release,
              library
            );

            this.gprsharp_now = GetMethod<Delegates.gprsharp_now_delegate>(
              LibBridgeExt.gprsharp_now,
              LibBridgeInternal.gprsharp_now,
              library
            );
            this.gprsharp_inf_future = GetMethod<Delegates.gprsharp_inf_future_delegate>(
              LibBridgeExt.gprsharp_inf_future,
              LibBridgeInternal.gprsharp_inf_future,
              library
            );
            this.gprsharp_inf_past = GetMethod<Delegates.gprsharp_inf_past_delegate>(
              LibBridgeExt.gprsharp_inf_past,
              LibBridgeInternal.gprsharp_inf_past,
              library
            );
            this.gprsharp_convert_clock_type = GetMethod<Delegates.gprsharp_convert_clock_type_delegate>(
              LibBridgeExt.gprsharp_convert_clock_type,
              LibBridgeInternal.gprsharp_convert_clock_type,
              library
            );
            this.gprsharp_sizeof_timespec = GetMethod<Delegates.gprsharp_sizeof_timespec_delegate>(
              LibBridgeExt.gprsharp_sizeof_timespec,
              LibBridgeInternal.gprsharp_sizeof_timespec,
              library
            );

            this.grpcsharp_test_callback = GetMethod<Delegates.grpcsharp_test_callback_delegate>(
              LibBridgeExt.grpcsharp_test_callback,
              LibBridgeInternal.grpcsharp_test_callback,
              library
            );
            this.grpcsharp_test_nop = GetMethod<Delegates.grpcsharp_test_nop_delegate>(
              LibBridgeExt.grpcsharp_test_nop,
              LibBridgeInternal.grpcsharp_test_nop,
              library
            );
            this.grpcsharp_test_override_method = GetMethod<Delegates.grpcsharp_test_override_method_delegate>(
              LibBridgeExt.grpcsharp_test_override_method,
              LibBridgeInternal.grpcsharp_test_override_method,
              library
            );
        }

        /// <summary>
        /// Gets singleton instance of this class.
        /// </summary>
        public static NativeMethods Get()
        {
            return NativeExtension.Get().NativeMethods;
        }

        /// <summary>
        /// Instead of loading from the UnmanagedLibrary, if the flag is set,
        /// </summary>
        public static T GetMethod<T>(
            T extMethod, T internalMethod, UnmanagedLibrary library
        ) where T : class {
            switch (LibBridge.type) {
                case LibBridgeType.Ext: return extMethod;
                case LibBridgeType.Internal: return internalMethod;
                case LibBridgeType.UnmanagedLibrary: return GetMethodDelegate<T>(library);
                default: return GetMethodDelegate<T>(library);
            }
        }

        static T GetMethodDelegate<T>(UnmanagedLibrary library)
            where T : class {
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
            public delegate IntPtr grpcsharp_batch_context_recv_status_on_client_details_delegate(BatchContextSafeHandle ctx, out UIntPtr detailsLength);
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
            public delegate void grpcsharp_request_call_context_destroy_delegate(IntPtr ctx);

            public delegate CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create_delegate(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);
            public delegate void grpcsharp_call_credentials_release_delegate(IntPtr credentials);

            public delegate CallError grpcsharp_call_cancel_delegate(CallSafeHandle call);
            public delegate CallError grpcsharp_call_cancel_with_status_delegate(CallSafeHandle call, StatusCode status, string description);
            public delegate CallError grpcsharp_call_start_unary_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            public delegate CallError grpcsharp_call_start_client_streaming_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            public delegate CallError grpcsharp_call_start_server_streaming_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, WriteFlags writeFlags,
                MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            public delegate CallError grpcsharp_call_start_duplex_streaming_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);
            public delegate CallError grpcsharp_call_send_message_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, WriteFlags writeFlags, int sendEmptyInitialMetadata);
            public delegate CallError grpcsharp_call_send_close_from_client_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx);
            public delegate CallError grpcsharp_call_send_status_from_server_delegate(CallSafeHandle call,
                BatchContextSafeHandle ctx, StatusCode statusCode, byte[] statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata,
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

            public delegate CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin_delegate(NativeMetadataInterceptor interceptor);
            public delegate void grpcsharp_metadata_credentials_notify_from_plugin_delegate(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);

            public delegate ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create_delegate(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, int forceClientAuth);
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

            public delegate Timespec gprsharp_now_delegate(ClockType clockType);
            public delegate Timespec gprsharp_inf_future_delegate(ClockType clockType);
            public delegate Timespec gprsharp_inf_past_delegate(ClockType clockType);

            public delegate Timespec gprsharp_convert_clock_type_delegate(Timespec t, ClockType targetClock);
            public delegate int gprsharp_sizeof_timespec_delegate();

            public delegate CallError grpcsharp_test_callback_delegate([MarshalAs(UnmanagedType.FunctionPtr)] NativeCallbackTestDelegate callback);
            public delegate IntPtr grpcsharp_test_nop_delegate(IntPtr ptr);
            public delegate void grpcsharp_test_override_method_delegate(string methodName, string variant);
        }
    }
}
