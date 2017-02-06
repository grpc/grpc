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
