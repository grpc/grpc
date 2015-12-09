using System;
using System.Runtime.InteropServices;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Platform invocation dispatch
    /// </summary>
    internal static class PlatformInvocation
    {
        /// <summary>
        /// Retrieve an implementation of platform invocations specific to the current cpu's architecture.
        /// This allows assemblies to function correctly when compiled for AnyCPU
        /// </summary>
        internal static IPlatformInvocation Implementation { get; private set; }

        static PlatformInvocation()
        {
            const string reasonTemplate = "{0} bindings are not available.";

            switch (Environment.OSVersion.Platform)
            {
                case PlatformID.MacOSX:
                case PlatformID.Unix:
                    Implementation = new PlatformInvocationLinux();
                    break;

                case PlatformID.Xbox:
                    throw new NotSupportedException(String.Format(reasonTemplate, "XBox"));

                default:
                    Implementation = Environment.Is64BitProcess ? (IPlatformInvocation)new PlatformInvocationWin64() : new PlatformInvocationWin32();
                    break;
            }
        }
    }

    internal interface IPlatformInvocation
    {
        // Environment 

        void grpcsharp_init();

        void grpcsharp_shutdown();

        IntPtr grpcsharp_version_string();


        // BatchContextSafeHandle

        BatchContextSafeHandle grpcsharp_batch_context_create();

        IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);

        IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);

        void grpcsharp_batch_context_recv_message_to_buffer(BatchContextSafeHandle ctx, byte[] buffer, UIntPtr bufferLen);

        StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);

        IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx); // returns const char*

        IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);

        CallSafeHandle grpcsharp_batch_context_server_rpc_new_call(BatchContextSafeHandle ctx);

        IntPtr grpcsharp_batch_context_server_rpc_new_method(BatchContextSafeHandle ctx); // returns const char*

        IntPtr grpcsharp_batch_context_server_rpc_new_host(BatchContextSafeHandle ctx); // returns const char*

        Timespec grpcsharp_batch_context_server_rpc_new_deadline(BatchContextSafeHandle ctx);

        IntPtr grpcsharp_batch_context_server_rpc_new_request_metadata(BatchContextSafeHandle ctx);

        int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);

        void grpcsharp_batch_context_destroy(IntPtr ctx);

        // CallCredentialsSafeHandle
        CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);

        void grpcsharp_call_credentials_release(IntPtr credentials);

        // CallSafeHandle

        GRPCCallError grpcsharp_call_cancel(CallSafeHandle call);

        GRPCCallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);
        
        GRPCCallError grpcsharp_call_start_unary(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len, MetadataArraySafeHandle metadataArray, WriteFlags writeFlags);
        
        GRPCCallError grpcsharp_call_start_client_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
        
        GRPCCallError grpcsharp_call_start_server_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len,
            MetadataArraySafeHandle metadataArray, WriteFlags writeFlags);
        
        GRPCCallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
        
        GRPCCallError grpcsharp_call_send_message(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len, WriteFlags writeFlags, bool sendEmptyInitialMetadata);
        
        GRPCCallError grpcsharp_call_send_close_from_client(CallSafeHandle call,
            BatchContextSafeHandle ctx);
        
        GRPCCallError grpcsharp_call_send_status_from_server(CallSafeHandle call,
            BatchContextSafeHandle ctx, StatusCode statusCode, string statusMessage, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata);
        
        GRPCCallError grpcsharp_call_recv_message(CallSafeHandle call,
            BatchContextSafeHandle ctx);
        
        GRPCCallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call,
            BatchContextSafeHandle ctx);

        GRPCCallError grpcsharp_call_start_serverside(CallSafeHandle call,
            BatchContextSafeHandle ctx);

        GRPCCallError grpcsharp_call_send_initial_metadata(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);
        
        GRPCCallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);
        
        CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);

        void grpcsharp_call_destroy(IntPtr call);


        // ChannelArgsSafeHandle
        ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);

        void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key,
            string value);

        void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index,
            string key, int value);

        void grpcsharp_channel_args_destroy(IntPtr args);

        // ChannelCredentialsSafeHandle

        ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey);

        ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);

        void grpcsharp_channel_credentials_release(IntPtr credentials);

        // ChannelSafeHandle

        ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);

        ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);

        CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);

        ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);

        void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

        CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);

        void grpcsharp_channel_destroy(IntPtr channel);

        // CompletionQueueEvent

        int grpcsharp_sizeof_grpc_event();

        // CompletionQueueSafeHandle

        CompletionQueueSafeHandle grpcsharp_completion_queue_create();

        void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);
        CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);

        CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);

        void grpcsharp_completion_queue_destroy(IntPtr cq);



        // CStringSafeHandle 

        void gprsharp_free(IntPtr ptr);



        // MetadataArraySafeHandle

        MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);

        void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);

        UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);

        IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index);

        IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index);

        UIntPtr grpcsharp_metadata_array_get_value_length(IntPtr metadataArray, UIntPtr index);

        void grpcsharp_metadata_array_destroy_full(IntPtr array);


        // NativeLogRedirector

        void grpcsharp_redirect_log(GprLogDelegate callback);

        // NativeMetadataCredentialsPlugin

        CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(NativeMetadataInterceptor interceptor);

        void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);



        // ServerCredentialsSafeHandle

        ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts,
            string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs,
            bool forceClientAuth);

        void grpcsharp_server_credentials_release(IntPtr credentials);

        // ServerSafeHandle

        ServerSafeHandle grpcsharp_server_create(CompletionQueueSafeHandle cq, ChannelArgsSafeHandle args);

        int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);

        int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);

        void grpcsharp_server_start(ServerSafeHandle server);

        GRPCCallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

        void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);

        void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

        void grpcsharp_server_destroy(IntPtr server);

        // Timespec

        Timespec gprsharp_now(GPRClockType clockType);

        Timespec gprsharp_inf_future(GPRClockType clockType);

        Timespec gprsharp_inf_past(GPRClockType clockType);

        Timespec gprsharp_convert_clock_type(Timespec t, GPRClockType targetClock);

        int gprsharp_sizeof_timespec();

    }
}
