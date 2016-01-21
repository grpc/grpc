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
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Profiling;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_channel from <c>grpc/grpc.h</c>
    /// </summary>
    internal class ChannelSafeHandle : SafeHandleZeroIsInvalid
    {
        [DllImport("grpc_csharp_ext.dll")]
        static extern ChannelSafeHandle grpcsharp_insecure_channel_create(string target, ChannelArgsSafeHandle channelArgs);

        [DllImport("grpc_csharp_ext.dll")]
        static extern ChannelSafeHandle grpcsharp_secure_channel_create(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs);

        [DllImport("grpc_csharp_ext.dll")]
        static extern CallSafeHandle grpcsharp_channel_create_call(ChannelSafeHandle channel, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline);

        [DllImport("grpc_csharp_ext.dll")]
        static extern ChannelState grpcsharp_channel_check_connectivity_state(ChannelSafeHandle channel, int tryToConnect);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_channel_watch_connectivity_state(ChannelSafeHandle channel, ChannelState lastObservedState,
            Timespec deadline, CompletionQueueSafeHandle cq, BatchContextSafeHandle ctx);

        [DllImport("grpc_csharp_ext.dll")]
        static extern CStringSafeHandle grpcsharp_channel_get_target(ChannelSafeHandle call);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_channel_destroy(IntPtr channel);

        private ChannelSafeHandle()
        {
        }

        public static ChannelSafeHandle CreateInsecure(string target, ChannelArgsSafeHandle channelArgs)
        {
            // Increment reference count for the native gRPC environment to make sure we don't do grpc_shutdown() before destroying the server handle.
            // Doing so would make object finalizer crash if we end up abandoning the handle.
            GrpcEnvironment.GrpcNativeInit();
            return grpcsharp_insecure_channel_create(target, channelArgs);
        }

        public static ChannelSafeHandle CreateSecure(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs)
        {
            // Increment reference count for the native gRPC environment to make sure we don't do grpc_shutdown() before destroying the server handle.
            // Doing so would make object finalizer crash if we end up abandoning the handle.
            GrpcEnvironment.GrpcNativeInit();
            return grpcsharp_secure_channel_create(credentials, target, channelArgs);
        }

        public CallSafeHandle CreateCall(CompletionRegistry registry, CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline, CallCredentialsSafeHandle credentials)
        {
            using (Profilers.ForCurrentThread().NewScope("ChannelSafeHandle.CreateCall"))
            {
                var result = grpcsharp_channel_create_call(this, parentCall, propagationMask, cq, method, host, deadline);
                if (credentials != null)
                {
                    result.SetCredentials(credentials);
                }
                result.SetCompletionRegistry(registry);
                return result;
            }
        }

        public ChannelState CheckConnectivityState(bool tryToConnect)
        {
            return grpcsharp_channel_check_connectivity_state(this, tryToConnect ? 1 : 0);
        }

        public void WatchConnectivityState(ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq,
            CompletionRegistry completionRegistry, BatchCompletionDelegate callback)
        {
            var ctx = BatchContextSafeHandle.Create();
            completionRegistry.RegisterBatchCompletion(ctx, callback);
            grpcsharp_channel_watch_connectivity_state(this, lastObservedState, deadline, cq, ctx);
        }

        public string GetTarget()
        {
            using (var cstring = grpcsharp_channel_get_target(this))
            {
                return cstring.GetValue();
            }
        }

        protected override bool ReleaseHandle()
        {
            grpcsharp_channel_destroy(handle);
            GrpcEnvironment.GrpcNativeShutdown();
            return true;
        }
    }
}
