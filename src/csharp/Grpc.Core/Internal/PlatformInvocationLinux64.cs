using System;
using System.Runtime.InteropServices;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Platform invoke method bindings for 64-bit Linux platforms
    /// </summary>
    internal class PlatformInvocationLinux64 : IPlatformInvocation
    {
        // Environment

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_init();

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_shutdown();

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpcsharp_version_string();  // returns not-owned const char*

        void IPlatformInvocation.grpcsharp_init()
        {
            grpcsharp_init();
        }

        void IPlatformInvocation.grpcsharp_shutdown()
        {
            grpcsharp_shutdown();
        }

        IntPtr IPlatformInvocation.grpcsharp_version_string()
        {
            return grpcsharp_version_string();
        }

        // BatchContextSafeHandle

        [DllImport("libgrpc_csharp_ext.so")]
        static extern BatchContextSafeHandle grpcsharp_batch_context_create();

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_batch_context_recv_message_to_buffer(BatchContextSafeHandle ctx, byte[] buffer, UIntPtr bufferLen);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx);  // returns const char*

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern CallSafeHandle grpcsharp_batch_context_server_rpc_new_call(BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpcsharp_batch_context_server_rpc_new_method(BatchContextSafeHandle ctx);  // returns const char*

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpcsharp_batch_context_server_rpc_new_host(BatchContextSafeHandle ctx);  // returns const char*

        [DllImport("libgrpc_csharp_ext.so")]
        static extern Timespec grpcsharp_batch_context_server_rpc_new_deadline(BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpcsharp_batch_context_server_rpc_new_request_metadata(BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_batch_context_destroy(IntPtr ctx);



        BatchContextSafeHandle IPlatformInvocation.grpcsharp_batch_context_create()
        {
            return grpcsharp_batch_context_create();
        }

        IntPtr IPlatformInvocation.grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx)
        {
            return grpcsharp_batch_context_recv_initial_metadata(ctx);
        }

        IntPtr IPlatformInvocation.grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx)
        {
            return grpcsharp_batch_context_recv_message_length(ctx);
        }

        void IPlatformInvocation.grpcsharp_batch_context_recv_message_to_buffer(BatchContextSafeHandle ctx, byte[] buffer, UIntPtr bufferLen)
        {
            grpcsharp_batch_context_recv_message_to_buffer(ctx, buffer, bufferLen);
        }

        StatusCode IPlatformInvocation.grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx)
        {
            return grpcsharp_batch_context_recv_status_on_client_status(ctx);
        }

        IntPtr IPlatformInvocation.grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx) // returns const char*
        {
            return grpcsharp_batch_context_recv_status_on_client_details(ctx);
        }

        IntPtr IPlatformInvocation.grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx)
        {
            return grpcsharp_batch_context_recv_status_on_client_trailing_metadata(ctx);
        }

        CallSafeHandle IPlatformInvocation.grpcsharp_batch_context_server_rpc_new_call(BatchContextSafeHandle ctx)
        {
            return grpcsharp_batch_context_server_rpc_new_call(ctx);
        }

        IntPtr IPlatformInvocation.grpcsharp_batch_context_server_rpc_new_method(BatchContextSafeHandle ctx) // returns const char*
        {
            return grpcsharp_batch_context_server_rpc_new_method(ctx);
        }

        IntPtr IPlatformInvocation.grpcsharp_batch_context_server_rpc_new_host(BatchContextSafeHandle ctx) // returns const char*
        {
            return grpcsharp_batch_context_server_rpc_new_host(ctx);
        }

        Timespec IPlatformInvocation.grpcsharp_batch_context_server_rpc_new_deadline(BatchContextSafeHandle ctx)
        {
            return grpcsharp_batch_context_server_rpc_new_deadline(ctx);
        }

        IntPtr IPlatformInvocation.grpcsharp_batch_context_server_rpc_new_request_metadata(BatchContextSafeHandle ctx)
        {
            return grpcsharp_batch_context_server_rpc_new_request_metadata(ctx);
        }

        int IPlatformInvocation.grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx)
        {
            return grpcsharp_batch_context_recv_close_on_server_cancelled(ctx);
        }

        void IPlatformInvocation.grpcsharp_batch_context_destroy(IntPtr ctx)
        {
            grpcsharp_batch_context_destroy(ctx);
        }

        // CallCredentialsSafeHandle
        [DllImport("libgrpc_csharp_ext.so")]
        static extern CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_call_credentials_release(IntPtr credentials);

        CallCredentialsSafeHandle IPlatformInvocation.grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2)
        {
            return grpcsharp_composite_call_credentials_create(creds1, creds2);
        }

        void IPlatformInvocation.grpcsharp_call_credentials_release(IntPtr credentials)
        {
            grpcsharp_call_credentials_release(credentials);
        }


        // CallSafeHandle

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_cancel(CallSafeHandle call);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_start_unary(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len, MetadataArraySafeHandle metadataArray, WriteFlags writeFlags);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_start_client_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_start_server_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len,
            MetadataArraySafeHandle metadataArray, WriteFlags writeFlags);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_send_message(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len, WriteFlags writeFlags, bool sendEmptyInitialMetadata);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_send_close_from_client(CallSafeHandle call,
            BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_send_status_from_server(CallSafeHandle call,
            BatchContextSafeHandle ctx, StatusCode statusCode, string statusMessage, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_recv_message(CallSafeHandle call,
            BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call,
            BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_start_serverside(CallSafeHandle call,
            BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_send_initial_metadata(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_call_destroy(IntPtr call);



        GRPCCallError IPlatformInvocation.grpcsharp_call_cancel(CallSafeHandle call)
        {
            return grpcsharp_call_cancel(call);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status,
            string description)
        {
            return grpcsharp_call_cancel_with_status(call, status, description);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_start_unary(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len,
            MetadataArraySafeHandle metadataArray, WriteFlags writeFlags)
        {
            return grpcsharp_call_start_unary(call, ctx, send_buffer, send_buffer_len, metadataArray, writeFlags);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_start_client_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray)
        {
            return grpcsharp_call_start_client_streaming(call, ctx, metadataArray);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_start_server_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len,
            MetadataArraySafeHandle metadataArray, WriteFlags writeFlags)
        {
            return grpcsharp_call_start_server_streaming(call, ctx, send_buffer, send_buffer_len, metadataArray,
                writeFlags);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_start_duplex_streaming(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray)
        {
            return grpcsharp_call_start_duplex_streaming(call, ctx, metadataArray);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_send_message(CallSafeHandle call,
            BatchContextSafeHandle ctx, byte[] send_buffer, UIntPtr send_buffer_len, WriteFlags writeFlags,
            bool sendEmptyInitialMetadata)
        {
            return grpcsharp_call_send_message(call, ctx, send_buffer, send_buffer_len, writeFlags,
                sendEmptyInitialMetadata);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_send_close_from_client(CallSafeHandle call,
            BatchContextSafeHandle ctx)
        {
            return grpcsharp_call_send_close_from_client(call, ctx);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_send_status_from_server(CallSafeHandle call,
            BatchContextSafeHandle ctx, StatusCode statusCode, string statusMessage,
            MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata)
        {
            return grpcsharp_call_send_status_from_server(call, ctx, statusCode, statusMessage, metadataArray,
                sendEmptyInitialMetadata);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_recv_message(CallSafeHandle call,
            BatchContextSafeHandle ctx)
        {
            return grpcsharp_call_recv_message(call, ctx);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_recv_initial_metadata(CallSafeHandle call,
            BatchContextSafeHandle ctx)
        {
            return grpcsharp_call_recv_initial_metadata(call, ctx);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_start_serverside(CallSafeHandle call,
            BatchContextSafeHandle ctx)
        {
            return grpcsharp_call_start_serverside(call, ctx);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_send_initial_metadata(CallSafeHandle call,
            BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray)
        {
            return grpcsharp_call_send_initial_metadata(call, ctx, metadataArray);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_call_set_credentials(CallSafeHandle call,
            CallCredentialsSafeHandle credentials)
        {
            return grpcsharp_call_set_credentials(call, credentials);
        }

        CStringSafeHandle IPlatformInvocation.grpcsharp_call_get_peer(CallSafeHandle call)
        {
            return grpcsharp_call_get_peer(call);
        }

        void IPlatformInvocation.grpcsharp_call_destroy(IntPtr call)
        {
            grpcsharp_call_destroy(call);
        }






        // ChannelArgsSafeHandle 

        [DllImport("libgrpc_csharp_ext.so")]
        static extern ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);

        [DllImport("libgrpc_csharp_ext.so", CharSet = CharSet.Ansi)]
        static extern void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);

        [DllImport("libgrpc_csharp_ext.so", CharSet = CharSet.Ansi)]
        static extern void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_channel_args_destroy(IntPtr args);


        ChannelArgsSafeHandle IPlatformInvocation.grpcsharp_channel_args_create(UIntPtr numArgs)
        {
            return grpcsharp_channel_args_create(numArgs);
        }

        void IPlatformInvocation.grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key,
            string value)
        {
            grpcsharp_channel_args_set_string(args, index, key, value);
        }

        void IPlatformInvocation.grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index,
            string key, int value)
        {
            grpcsharp_channel_args_set_integer(args, index, key, value);
        }

        void IPlatformInvocation.grpcsharp_channel_args_destroy(IntPtr args)
        {
            grpcsharp_channel_args_destroy(args);
        }


        // ChannelCredentialsSafeHandle

        [DllImport("libgrpc_csharp_ext.so", CharSet = CharSet.Ansi)]
        static extern ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_channel_credentials_release(IntPtr credentials);

        ChannelCredentialsSafeHandle IPlatformInvocation.grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain,
            string keyCertPairPrivateKey)
        {
            return grpcsharp_ssl_credentials_create(pemRootCerts, keyCertPairCertChain, keyCertPairPrivateKey);
        }

        ChannelCredentialsSafeHandle IPlatformInvocation.grpcsharp_composite_channel_credentials_create(
            ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds)
        {
            return grpcsharp_composite_channel_credentials_create(channelCreds, callCreds);
        }

        void IPlatformInvocation.grpcsharp_channel_credentials_release(IntPtr credentials)
        {
            grpcsharp_channel_credentials_release(credentials);
        }


        // ChannelSafeHandle


        [DllImport("libgrpc_csharp_ext.so")]
        static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState,
            Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_channel_destroy(IntPtr channel);

        ChannelSafeHandle IPlatformInvocation.grpcsharp_insecure_channel_create(string target,
            ChannelArgsSafeHandle channelArgs)
        {
            return grpcsharp_insecure_channel_create(target, channelArgs);
        }

        ChannelSafeHandle IPlatformInvocation.grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials,
            string target, ChannelArgsSafeHandle channelArgs)
        {
            return grpcsharp_secure_channel_create(credentials, target, channelArgs);
        }

        CallSafeHandle IPlatformInvocation.grpcsharp_channel_create_call(ChannelSafeHandle channel,
            CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq,
            string method, string host, Timespec deadline)
        {
            return grpcsharp_channel_create_call(channel, parentCall, propagationMask, cq, method, host, deadline);
        }

        ChannelState IPlatformInvocation.grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect)
        {
            return grpcsharp_channel_check_connectivity_state(channel, tryToConnect);
        }

        void IPlatformInvocation.grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel,
            ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx)
        {
            grpcsharp_channel_watch_connectivity_state(channel, lastObservedState, deadline, cq, ctx);
        }

        CStringSafeHandle IPlatformInvocation.grpcsharp_channel_get_target(ChannelSafeHandle call)
        {
            return grpcsharp_channel_get_target(call);
        }

        void IPlatformInvocation.grpcsharp_channel_destroy(IntPtr channel)
        {
            grpcsharp_channel_destroy(channel);
        }


        // CompletionQueueEvent

        [DllImport("libgrpc_csharp_ext.so")]
        static extern int grpcsharp_sizeof_grpc_event();

        int IPlatformInvocation.grpcsharp_sizeof_grpc_event()
        {
            return grpcsharp_sizeof_grpc_event();
        }


        // CompletionQueueSafeHandle

        [DllImport("libgrpc_csharp_ext.so")]
        static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create();

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_completion_queue_destroy(IntPtr cq);

        CompletionQueueSafeHandle IPlatformInvocation.grpcsharp_completion_queue_create()
        {
            return grpcsharp_completion_queue_create();
        }

        void IPlatformInvocation.grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq)
        {
            grpcsharp_completion_queue_shutdown(cq);
        }
        CompletionQueueEvent IPlatformInvocation.grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq)
        {
            return grpcsharp_completion_queue_next(cq);
        }

        CompletionQueueEvent IPlatformInvocation.grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq,
            IntPtr tag)
        {
            return grpcsharp_completion_queue_pluck(cq, tag);
        }

        void IPlatformInvocation.grpcsharp_completion_queue_destroy(IntPtr cq)
        {
            grpcsharp_completion_queue_destroy(cq);
        }



        // CStringSafeHandle 

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void gprsharp_free(IntPtr ptr);

        void IPlatformInvocation.gprsharp_free(IntPtr ptr)
        {
            gprsharp_free(ptr);
        }


        // MetadataArraySafeHandle

        [DllImport("libgrpc_csharp_ext.so")]
        static extern MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);

        [DllImport("libgrpc_csharp_ext.so", CharSet = CharSet.Ansi)]
        static extern void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern UIntPtr grpcsharp_metadata_array_get_value_length(IntPtr metadataArray, UIntPtr index);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_metadata_array_destroy_full(IntPtr array);


        MetadataArraySafeHandle IPlatformInvocation.grpcsharp_metadata_array_create(UIntPtr capacity)
        {
            return grpcsharp_metadata_array_create(capacity);
        }

        void IPlatformInvocation.grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value,
            UIntPtr valueLength)
        {
            grpcsharp_metadata_array_add(array, key, value, valueLength);
        }

        UIntPtr IPlatformInvocation.grpcsharp_metadata_array_count(IntPtr metadataArray)
        {
            return grpcsharp_metadata_array_count(metadataArray);
        }

        IntPtr IPlatformInvocation.grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index)
        {
            return grpcsharp_metadata_array_get_key(metadataArray, index);
        }

        IntPtr IPlatformInvocation.grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index)
        {
            return grpcsharp_metadata_array_get_value(metadataArray, index);
        }

        UIntPtr IPlatformInvocation.grpcsharp_metadata_array_get_value_length(IntPtr metadataArray, UIntPtr index)
        {
            return grpcsharp_metadata_array_get_value_length(metadataArray, index);
        }

        void IPlatformInvocation.grpcsharp_metadata_array_destroy_full(IntPtr array)
        {
            grpcsharp_metadata_array_destroy_full(array);
        }



        // NativeLogRedirector

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_redirect_log(GprLogDelegate callback);

        void IPlatformInvocation.grpcsharp_redirect_log(GprLogDelegate callback)
        {
            grpcsharp_redirect_log(callback);
        }



        // NativeMetadataCredentialsPlugin

        [DllImport("libgrpc_csharp_ext.so")]
        static extern CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(NativeMetadataInterceptor interceptor);

        [DllImport("libgrpc_csharp_ext.so", CharSet = CharSet.Ansi)]
        static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);

        CallCredentialsSafeHandle IPlatformInvocation.grpcsharp_metadata_credentials_create_from_plugin(
            NativeMetadataInterceptor interceptor)
        {
            return grpcsharp_metadata_credentials_create_from_plugin(interceptor);
        }

        void IPlatformInvocation.grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData,
            MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails)
        {
            grpcsharp_metadata_credentials_notify_from_plugin(callbackPtr, userData, metadataArray, statusCode,
                errorDetails);
        }


        // ServerCredentialsSafeHandle

        [DllImport("libgrpc_csharp_ext.so", CharSet = CharSet.Ansi)]
        static extern ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, bool forceClientAuth);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_server_credentials_release(IntPtr credentials);

        ServerCredentialsSafeHandle IPlatformInvocation.grpcsharp_ssl_server_credentials_create(string pemRootCerts,
            string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs,
            bool forceClientAuth)
        {
            return grpcsharp_ssl_server_credentials_create(pemRootCerts, keyCertPairCertChainArray,
                keyCertPairPrivateKeyArray, numKeyCertPairs, forceClientAuth);
        }

        void IPlatformInvocation.grpcsharp_server_credentials_release(IntPtr credentials)
        {
            grpcsharp_server_credentials_release(credentials);
        }

        // ServerSafeHandle

        [DllImport("libgrpc_csharp_ext.so")]
        static extern ServerSafeHandle grpcsharp_server_create(CompletionQueueSafeHandle cq, ChannelArgsSafeHandle args);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_server_start(ServerSafeHandle server);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern GRPCCallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern void grpcsharp_server_destroy(IntPtr server);


        ServerSafeHandle IPlatformInvocation.grpcsharp_server_create(CompletionQueueSafeHandle cq,
            ChannelArgsSafeHandle args)
        {
            return grpcsharp_server_create(cq, args);
        }

        int IPlatformInvocation.grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr)
        {
            return grpcsharp_server_add_insecure_http2_port(server, addr);
        }

        int IPlatformInvocation.grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr,
            ServerCredentialsSafeHandle creds)
        {
            return grpcsharp_server_add_secure_http2_port(server, addr, creds);
        }

        void IPlatformInvocation.grpcsharp_server_start(ServerSafeHandle server)
        {
            grpcsharp_server_start(server);
        }

        GRPCCallError IPlatformInvocation.grpcsharp_server_request_call(ServerSafeHandle server,
            CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx)
        {
            return grpcsharp_server_request_call(server, cq, ctx);
        }

        void IPlatformInvocation.grpcsharp_server_cancel_all_calls(ServerSafeHandle server)
        {
            grpcsharp_server_cancel_all_calls(server);
        }

        void IPlatformInvocation.grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server,
            CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx)
        {
            grpcsharp_server_shutdown_and_notify_callback(server, cq, ctx);
        }

        void IPlatformInvocation.grpcsharp_server_destroy(IntPtr server)
        {
            grpcsharp_server_destroy(server);
        }



        // Timespec

        [DllImport("libgrpc_csharp_ext.so")]
        static extern Timespec gprsharp_now(GPRClockType clockType);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern Timespec gprsharp_inf_future(GPRClockType clockType);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern Timespec gprsharp_inf_past(GPRClockType clockType);
        [DllImport("libgrpc_csharp_ext.so")]
        static extern Timespec gprsharp_convert_clock_type(Timespec t, GPRClockType targetClock);

        [DllImport("libgrpc_csharp_ext.so")]
        static extern int gprsharp_sizeof_timespec();


        Timespec IPlatformInvocation.gprsharp_now(GPRClockType clockType)
        {
            return gprsharp_now(clockType);
        }

        Timespec IPlatformInvocation.gprsharp_inf_future(GPRClockType clockType)
        {
            return gprsharp_inf_future(clockType);
        }

        Timespec IPlatformInvocation.gprsharp_inf_past(GPRClockType clockType)
        {
            return gprsharp_inf_past(clockType);
        }

        Timespec IPlatformInvocation.gprsharp_convert_clock_type(Timespec t, GPRClockType targetClock)
        {
            return gprsharp_convert_clock_type(t, targetClock);
        }

        int IPlatformInvocation.gprsharp_sizeof_timespec()
        {
            return gprsharp_sizeof_timespec();
        }
    }
}
