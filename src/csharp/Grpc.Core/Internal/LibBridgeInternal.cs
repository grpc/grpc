using System;
using System.Runtime.InteropServices;

namespace Grpc.Core.Internal {
  internal static class LibBridgeInternal {
    private const string pluginName = "__Internal";

    [DllImport(pluginName)]
    internal static extern void grpcsharp_init();

    [DllImport(pluginName)]
    internal static extern void grpcsharp_shutdown();

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_version_string();

    [DllImport(pluginName)]
    internal static extern BatchContextSafeHandle grpcsharp_batch_context_create();

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_batch_context_recv_initial_metadata(BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_batch_context_recv_message_length(BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_batch_context_recv_message_to_buffer(BatchContextSafeHandle ctx, byte[] buffer, UIntPtr bufferLen);

    [DllImport(pluginName)]
    internal static extern StatusCode grpcsharp_batch_context_recv_status_on_client_status(BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_batch_context_recv_status_on_client_details(BatchContextSafeHandle ctx, out UIntPtr detailsLength);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_batch_context_recv_status_on_client_trailing_metadata(BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern int grpcsharp_batch_context_recv_close_on_server_cancelled(BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_batch_context_reset(BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_batch_context_destroy(IntPtr ctx);


    [DllImport(pluginName)]
    internal static extern RequestCallContextSafeHandle grpcsharp_request_call_context_create();

    [DllImport(pluginName)]
    internal static extern CallSafeHandle grpcsharp_request_call_context_call(RequestCallContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_request_call_context_method(RequestCallContextSafeHandle ctx, out UIntPtr methodLength);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_request_call_context_host(RequestCallContextSafeHandle ctx, out UIntPtr hostLength);

    [DllImport(pluginName)]
    internal static extern Timespec grpcsharp_request_call_context_deadline(RequestCallContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_request_call_context_request_metadata(RequestCallContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_request_call_context_destroy(IntPtr ctx);


    [DllImport(pluginName)]
    internal static extern CallCredentialsSafeHandle grpcsharp_composite_call_credentials_create(CallCredentialsSafeHandle creds1, CallCredentialsSafeHandle creds2);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_call_credentials_release(IntPtr credentials);


    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_cancel(CallSafeHandle call);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_cancel_with_status(CallSafeHandle call, StatusCode status, string description);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_start_unary(CallSafeHandle call,
        BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_start_client_streaming(CallSafeHandle call,
        BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_start_server_streaming(CallSafeHandle call,
        BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, WriteFlags writeFlags,
        MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_start_duplex_streaming(CallSafeHandle call,
        BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_send_message(CallSafeHandle call,
        BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, WriteFlags writeFlags, int sendEmptyInitialMetadata);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_send_close_from_client(CallSafeHandle call,
        BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_send_status_from_server(CallSafeHandle call,
        BatchContextSafeHandle ctx, StatusCode statusCode, byte[] statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata,
        byte[] optionalSendBuffer, UIntPtr optionalSendBufferLen, WriteFlags writeFlags);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_recv_message(CallSafeHandle call,
        BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_recv_initial_metadata(CallSafeHandle call,
        BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_start_serverside(CallSafeHandle call,
        BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_send_initial_metadata(CallSafeHandle call,
        BatchContextSafeHandle ctx, MetadataArraySafeHandle metadataArray);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_call_set_credentials(CallSafeHandle call, CallCredentialsSafeHandle credentials);

    [DllImport(pluginName)]
    internal static extern CStringSafeHandle grpcsharp_call_get_peer(CallSafeHandle call);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_call_destroy(IntPtr call);


    [DllImport(pluginName)]
    internal static extern ChannelArgsSafeHandle grpcsharp_channel_args_create(UIntPtr numArgs);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_channel_args_set_string(ChannelArgsSafeHandle args, UIntPtr index, string key, string value);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_channel_args_set_integer(ChannelArgsSafeHandle args, UIntPtr index, string key, int value);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_channel_args_destroy(IntPtr args);


    [DllImport(pluginName)]
    internal static extern void grpcsharp_override_default_ssl_roots(string pemRootCerts);

    [DllImport(pluginName)]
    internal static extern ChannelCredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey);

    [DllImport(pluginName)]
    internal static extern ChannelCredentialsSafeHandle grpcsharp_composite_channel_credentials_create(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_channel_credentials_release(IntPtr credentials);


    [DllImport(pluginName)]
    internal static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);

    [DllImport(pluginName)]
    internal static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);

    [DllImport(pluginName)]
    internal static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);

    [DllImport(pluginName)]
    internal static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState,
        Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_channel_destroy(IntPtr channel);


    [DllImport(pluginName)]
    internal static extern int grpcsharp_sizeof_grpc_event();


    [DllImport(pluginName)]
    internal static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_async();

    [DllImport(pluginName)]
    internal static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create_sync();

    [DllImport(pluginName)]
    internal static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);

    [DllImport(pluginName)]
    internal static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);

    [DllImport(pluginName)]
    internal static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_completion_queue_destroy(IntPtr cq);


    [DllImport(pluginName)]
    internal static extern void gprsharp_free(IntPtr ptr);


    [DllImport(pluginName)]
    internal static extern MetadataArraySafeHandle grpcsharp_metadata_array_create(UIntPtr capacity);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_metadata_array_add(MetadataArraySafeHandle array, string key, byte[] value, UIntPtr valueLength);

    [DllImport(pluginName)]
    internal static extern UIntPtr grpcsharp_metadata_array_count(IntPtr metadataArray);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_metadata_array_get_key(IntPtr metadataArray, UIntPtr index, out UIntPtr keyLength);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_metadata_array_get_value(IntPtr metadataArray, UIntPtr index, out UIntPtr valueLength);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_metadata_array_destroy_full(IntPtr array);


    [DllImport(pluginName)]
    internal static extern void grpcsharp_redirect_log(GprLogDelegate callback);


    [DllImport(pluginName)]
    internal static extern CallCredentialsSafeHandle grpcsharp_metadata_credentials_create_from_plugin(NativeMetadataInterceptor interceptor);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_metadata_credentials_notify_from_plugin(IntPtr callbackPtr, IntPtr userData, MetadataArraySafeHandle metadataArray, StatusCode statusCode, string errorDetails);


    [DllImport(pluginName)]
    internal static extern ServerCredentialsSafeHandle grpcsharp_ssl_server_credentials_create(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, UIntPtr numKeyCertPairs, int forceClientAuth);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_server_credentials_release(IntPtr credentials);


    [DllImport(pluginName)]
    internal static extern ServerSafeHandle grpcsharp_server_create(ChannelArgsSafeHandle args);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_server_register_completion_queue(ServerSafeHandle server, CompletionQueueSafeHandle cq);

    [DllImport(pluginName)]
    internal static extern int grpcsharp_server_add_insecure_http2_port(ServerSafeHandle server, string addr);

    [DllImport(pluginName)]
    internal static extern int grpcsharp_server_add_secure_http2_port(ServerSafeHandle server, string addr, ServerCredentialsSafeHandle creds);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_server_start(ServerSafeHandle server);

    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_server_request_call(ServerSafeHandle server, CompletionQueueSafeHandle cq, RequestCallContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_server_cancel_all_calls(ServerSafeHandle server);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_server_shutdown_and_notify_callback(ServerSafeHandle server, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_server_destroy(IntPtr server);


    [DllImport(pluginName)]
    internal static extern AuthContextSafeHandle grpcsharp_call_auth_context(CallSafeHandle call);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_auth_context_peer_identity_property_name(AuthContextSafeHandle authContext);  // returns const char*

    [DllImport(pluginName)]
    internal static extern AuthContextSafeHandle.NativeAuthPropertyIterator grpcsharp_auth_context_property_iterator(AuthContextSafeHandle authContext);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_auth_property_iterator_next(ref AuthContextSafeHandle.NativeAuthPropertyIterator iterator);  // returns const auth_property*

    [DllImport(pluginName)]
    internal static extern void grpcsharp_auth_context_release(IntPtr authContext);


    [DllImport(pluginName)]
    internal static extern Timespec gprsharp_now(ClockType clockType);

    [DllImport(pluginName)]
    internal static extern Timespec gprsharp_inf_future(ClockType clockType);

    [DllImport(pluginName)]
    internal static extern Timespec gprsharp_inf_past(ClockType clockType);


    [DllImport(pluginName)]
    internal static extern Timespec gprsharp_convert_clock_type(Timespec t, ClockType targetClock);

    [DllImport(pluginName)]
    internal static extern int gprsharp_sizeof_timespec();


    [DllImport(pluginName)]
    internal static extern CallError grpcsharp_test_callback([MarshalAs(UnmanagedType.FunctionPtr)] NativeCallbackTestDelegate callback);

    [DllImport(pluginName)]
    internal static extern IntPtr grpcsharp_test_nop(IntPtr ptr);

    [DllImport(pluginName)]
    internal static extern void grpcsharp_test_override_method(string methodName, string variant);
  }
}
