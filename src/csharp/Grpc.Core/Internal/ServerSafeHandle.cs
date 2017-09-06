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

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_server from grpc/grpc.h
    /// </summary>
    internal sealed class ServerSafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        private ServerSafeHandle()
        {
        }

        public static ServerSafeHandle NewServer(ChannelArgsSafeHandle args)
        {
            // Increment reference count for the native gRPC environment to make sure we don't do grpc_shutdown() before destroying the server handle.
            // Doing so would make object finalizer crash if we end up abandoning the handle.
            GrpcEnvironment.GrpcNativeInit();
            return Native.grpcsharp_server_create(args);
        }

        public void RegisterCompletionQueue(CompletionQueueSafeHandle cq)
        {
            using (cq.NewScope())
            {
                Native.grpcsharp_server_register_completion_queue(this, cq);
            }
        }

        public int AddInsecurePort(string addr)
        {
            return Native.grpcsharp_server_add_insecure_http2_port(this, addr);
        }

        public int AddSecurePort(string addr, ServerCredentialsSafeHandle credentials)
        {
            return Native.grpcsharp_server_add_secure_http2_port(this, addr, credentials);
        }

        public void Start()
        {
            Native.grpcsharp_server_start(this);
        }
    
        public void ShutdownAndNotify(BatchCompletionDelegate callback, CompletionQueueSafeHandle completionQueue)
        {
            using (completionQueue.NewScope())
            {
                var ctx = BatchContextSafeHandle.Create();
                completionQueue.CompletionRegistry.RegisterBatchCompletion(ctx, callback);
                Native.grpcsharp_server_shutdown_and_notify_callback(this, completionQueue, ctx);
            }
        }

        public void RequestCall(RequestCallCompletionDelegate callback, CompletionQueueSafeHandle completionQueue)
        {
            using (completionQueue.NewScope())
            {
                var ctx = RequestCallContextSafeHandle.Create();
                completionQueue.CompletionRegistry.RegisterRequestCallCompletion(ctx, callback);
                Native.grpcsharp_server_request_call(this, completionQueue, ctx).CheckOk();
            }
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_server_destroy(handle);
            GrpcEnvironment.GrpcNativeShutdown();
            return true;
        }
            
        // Only to be called after ShutdownAndNotify.
        public void CancelAllCalls()
        {
            Native.grpcsharp_server_cancel_all_calls(this);
        }
    }
}
