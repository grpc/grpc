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
        static readonly NativeMethods Native = NativeMethods.Get();

        private ChannelSafeHandle()
        {
        }

        public static ChannelSafeHandle CreateInsecure(string target, ChannelArgsSafeHandle channelArgs)
        {
            // Increment reference count for the native gRPC environment to make sure we don't do grpc_shutdown() before destroying the server handle.
            // Doing so would make object finalizer crash if we end up abandoning the handle.
            GrpcEnvironment.GrpcNativeInit();
            return Native.grpcsharp_insecure_channel_create(target, channelArgs);
        }

        public static ChannelSafeHandle CreateSecure(ChannelCredentialsSafeHandle credentials, string target, ChannelArgsSafeHandle channelArgs)
        {
            // Increment reference count for the native gRPC environment to make sure we don't do grpc_shutdown() before destroying the server handle.
            // Doing so would make object finalizer crash if we end up abandoning the handle.
            GrpcEnvironment.GrpcNativeInit();
            return Native.grpcsharp_secure_channel_create(credentials, target, channelArgs);
        }

        public CallSafeHandle CreateCall(CallSafeHandle parentCall, ContextPropagationFlags propagationMask, CompletionQueueSafeHandle cq, string method, string host, Timespec deadline, CallCredentialsSafeHandle credentials)
        {
            var result = Native.grpcsharp_channel_create_call(this, parentCall, propagationMask, cq, method, host, deadline);
            if (credentials != null)
            {
                result.SetCredentials(credentials);
            }
            result.Initialize(cq);
            return result;
        }

        public ChannelState CheckConnectivityState(bool tryToConnect)
        {
            return Native.grpcsharp_channel_check_connectivity_state(this, tryToConnect ? 1 : 0);
        }

        public void WatchConnectivityState(ChannelState lastObservedState, Timespec deadline, CompletionQueueSafeHandle cq, BatchCompletionDelegate callback, object callbackState)
        {
            var ctx = BatchContextSafeHandle.Create();
            cq.CompletionRegistry.RegisterBatchCompletion(ctx, callback, callbackState);
            Native.grpcsharp_channel_watch_connectivity_state(this, lastObservedState, deadline, cq, ctx);
        }

        public string GetTarget()
        {
            using (var cstring = Native.grpcsharp_channel_get_target(this))
            {
                return cstring.GetValue();
            }
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_channel_destroy(handle);
            GrpcEnvironment.GrpcNativeShutdown();
            return true;
        }
    }
}
