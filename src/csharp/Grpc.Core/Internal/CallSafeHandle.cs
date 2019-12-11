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
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Core.Profiling;
using System.Buffers;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_call from <c>grpc/grpc.h</c>
    /// </summary>
    internal class CallSafeHandle : SafeHandleZeroIsInvalid, INativeCall
    {
        public static readonly CallSafeHandle NullInstance = new CallSafeHandle();
        static readonly NativeMethods Native = NativeMethods.Get();

        // Completion handlers are pre-allocated to avoid unnecessary delegate allocations.
        // The "state" field is used to store the actual callback to invoke.
        static readonly BatchCompletionDelegate CompletionHandler_IUnaryResponseClientCallback =
            (success, context, state) => ((IUnaryResponseClientCallback)state).OnUnaryResponseClient(success, context.GetReceivedStatusOnClient(), context.GetReceivedMessageReader(), context.GetReceivedInitialMetadata());
        static readonly BatchCompletionDelegate CompletionHandler_IReceivedStatusOnClientCallback =
            (success, context, state) => ((IReceivedStatusOnClientCallback)state).OnReceivedStatusOnClient(success, context.GetReceivedStatusOnClient());
        static readonly BatchCompletionDelegate CompletionHandler_IReceivedMessageCallback =
            (success, context, state) => ((IReceivedMessageCallback)state).OnReceivedMessage(success, context.GetReceivedMessageReader());
        static readonly BatchCompletionDelegate CompletionHandler_IReceivedResponseHeadersCallback =
            (success, context, state) => ((IReceivedResponseHeadersCallback)state).OnReceivedResponseHeaders(success, context.GetReceivedInitialMetadata());
        static readonly BatchCompletionDelegate CompletionHandler_ISendCompletionCallback =
            (success, context, state) => ((ISendCompletionCallback)state).OnSendCompletion(success);
        static readonly BatchCompletionDelegate CompletionHandler_ISendStatusFromServerCompletionCallback =
            (success, context, state) => ((ISendStatusFromServerCompletionCallback)state).OnSendStatusFromServerCompletion(success);
        static readonly BatchCompletionDelegate CompletionHandler_IReceivedCloseOnServerCallback =
            (success, context, state) => ((IReceivedCloseOnServerCallback)state).OnReceivedCloseOnServer(success, context.GetReceivedCloseOnServerCancelled());

        const uint GRPC_WRITE_BUFFER_HINT = 1;
        CompletionQueueSafeHandle completionQueue;

        private CallSafeHandle()
        {
        }

        public void Initialize(CompletionQueueSafeHandle completionQueue)
        {
            this.completionQueue = completionQueue;
        }

        public void SetCredentials(CallCredentialsSafeHandle credentials)
        {
            Native.grpcsharp_call_set_credentials(this, credentials).CheckOk();
        }

        public void StartUnary(IUnaryResponseClientCallback callback, SliceBufferSafeHandle payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_IUnaryResponseClientCallback, callback);
                Native.grpcsharp_call_start_unary(this, ctx, payload, writeFlags, metadataArray, callFlags)
                    .CheckOk();
            }
        }

        public void StartUnary(BatchContextSafeHandle ctx, SliceBufferSafeHandle payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            Native.grpcsharp_call_start_unary(this, ctx, payload, writeFlags, metadataArray, callFlags)
                .CheckOk();
        }

        public void StartClientStreaming(IUnaryResponseClientCallback callback, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_IUnaryResponseClientCallback, callback);
                Native.grpcsharp_call_start_client_streaming(this, ctx, metadataArray, callFlags).CheckOk();
            }
        }

        public void StartServerStreaming(IReceivedStatusOnClientCallback callback, SliceBufferSafeHandle payload, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_IReceivedStatusOnClientCallback, callback);
                Native.grpcsharp_call_start_server_streaming(this, ctx, payload, writeFlags, metadataArray, callFlags).CheckOk();
            }
        }

        public void StartDuplexStreaming(IReceivedStatusOnClientCallback callback, MetadataArraySafeHandle metadataArray, CallFlags callFlags)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_IReceivedStatusOnClientCallback, callback);
                Native.grpcsharp_call_start_duplex_streaming(this, ctx, metadataArray, callFlags).CheckOk();
            }
        }

        public void StartSendMessage(ISendCompletionCallback callback, SliceBufferSafeHandle payload, WriteFlags writeFlags, bool sendEmptyInitialMetadata)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_ISendCompletionCallback, callback);
                Native.grpcsharp_call_send_message(this, ctx, payload, writeFlags, sendEmptyInitialMetadata ? 1 : 0).CheckOk();
            }
        }

        public void StartSendCloseFromClient(ISendCompletionCallback callback)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_ISendCompletionCallback, callback);
                Native.grpcsharp_call_send_close_from_client(this, ctx).CheckOk();
            }
        }

        public void StartSendStatusFromServer(ISendStatusFromServerCompletionCallback callback, Status status, MetadataArraySafeHandle metadataArray, bool sendEmptyInitialMetadata,
            SliceBufferSafeHandle optionalPayload, WriteFlags writeFlags)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_ISendStatusFromServerCompletionCallback, callback);
                
                const int MaxStackAllocBytes = 256;
                int maxBytes = MarshalUtils.GetMaxByteCountUTF8(status.Detail);
                if (maxBytes > MaxStackAllocBytes)
                {
                    // pay the extra to get the *actual* size; this could mean that
                    // it ends up fitting on the stack after all, but even if not
                    // it will mean that we ask for a *much* smaller buffer
                    maxBytes = MarshalUtils.GetByteCountUTF8(status.Detail);
                }

                unsafe
                {
                    if (maxBytes <= MaxStackAllocBytes)
                    {   // for small status, we can encode on the stack without touching arrays
                        // note: if init-locals is disabled, it would be more efficient
                        // to just stackalloc[MaxStackAllocBytes]; but by default, since we
                        // expect this to be small and it needs to wipe, just use maxBytes
                        byte* ptr = stackalloc byte[maxBytes];
                        int statusBytes = MarshalUtils.GetBytesUTF8(status.Detail, ptr, maxBytes);
                        Native.grpcsharp_call_send_status_from_server(this, ctx, status.StatusCode, new IntPtr(ptr), new UIntPtr((ulong)statusBytes), metadataArray, sendEmptyInitialMetadata ? 1 : 0,
                            optionalPayload, writeFlags).CheckOk();
                    }
                    else
                    {   // for larger status (rare), rent a buffer from the pool and
                        // use that for encoding
                        var statusBuffer = ArrayPool<byte>.Shared.Rent(maxBytes);
                        try
                        {
                            fixed (byte* ptr = statusBuffer)
                            {
                                int statusBytes = MarshalUtils.GetBytesUTF8(status.Detail, ptr, maxBytes);
                                Native.grpcsharp_call_send_status_from_server(this, ctx, status.StatusCode, new IntPtr(ptr), new UIntPtr((ulong)statusBytes), metadataArray, sendEmptyInitialMetadata ? 1 : 0,
                                  optionalPayload, writeFlags).CheckOk();
                            }
                        }
                        finally
                        {
                            ArrayPool<byte>.Shared.Return(statusBuffer);
                        }
                    }
                }
            }
        }

        public void StartReceiveMessage(IReceivedMessageCallback callback)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_IReceivedMessageCallback, callback);
                Native.grpcsharp_call_recv_message(this, ctx).CheckOk();
            }
        }

        public void StartReceiveInitialMetadata(IReceivedResponseHeadersCallback callback)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_IReceivedResponseHeadersCallback, callback);
                Native.grpcsharp_call_recv_initial_metadata(this, ctx).CheckOk();
            }
        }

        public void StartServerSide(IReceivedCloseOnServerCallback callback)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_IReceivedCloseOnServerCallback, callback);
                Native.grpcsharp_call_start_serverside(this, ctx).CheckOk();
            }
        }

        public void StartSendInitialMetadata(ISendCompletionCallback callback, MetadataArraySafeHandle metadataArray)
        {
            using (completionQueue.NewScope())
            {
                var ctx = completionQueue.CompletionRegistry.RegisterBatchCompletion(CompletionHandler_ISendCompletionCallback, callback);
                Native.grpcsharp_call_send_initial_metadata(this, ctx, metadataArray).CheckOk();
            }
        }

        public void Cancel()
        {
            Native.grpcsharp_call_cancel(this).CheckOk();
        }

        public void CancelWithStatus(Status status)
        {
            Native.grpcsharp_call_cancel_with_status(this, status.StatusCode, status.Detail).CheckOk();
        }

        public string GetPeer()
        {
            using (var cstring = Native.grpcsharp_call_get_peer(this))
            {
                return cstring.GetValue();
            }
        }

        public AuthContextSafeHandle GetAuthContext()
        {
            return Native.grpcsharp_call_auth_context(this);
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_call_destroy(handle);
            return true;
        }

        private static uint GetFlags(bool buffered)
        {
            return buffered ? 0 : GRPC_WRITE_BUFFER_HINT;
        }

        /// <summary>
        /// Only for testing.
        /// </summary>
        public static CallSafeHandle CreateFake(IntPtr ptr, CompletionQueueSafeHandle cq)
        {
            var call = new CallSafeHandle();
            call.SetHandle(ptr);
            call.Initialize(cq);
            return call;
        }
    }
}
