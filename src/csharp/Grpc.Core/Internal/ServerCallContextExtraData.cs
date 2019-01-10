#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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
using System.Threading;
using System.Threading.Tasks;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Additional state for <c>ServerCallContext</c>.
    /// Storing the extra state outside of <c>ServerCallContext</c> allows it to be implementation-agnostic.
    /// </summary>
    internal class ServerCallContextExtraData
    {
        readonly CallSafeHandle callHandle;
        readonly IServerResponseStream serverResponseStream;
        readonly Lazy<AuthContext> cachedAuthContext;

        public ServerCallContextExtraData(CallSafeHandle callHandle, IServerResponseStream serverResponseStream)
        {
            this.callHandle = callHandle;
            this.serverResponseStream = serverResponseStream;
            // TODO(jtattermusch): avoid unnecessary allocation of factory function and the lazy object.
            this.cachedAuthContext = new Lazy<AuthContext>(GetAuthContextEager);
        }

        public ServerCallContext NewServerCallContext(ServerRpcNew newRpc, CancellationToken cancellationToken)
        {
            DateTime realtimeDeadline = newRpc.Deadline.ToClockType(ClockType.Realtime).ToDateTime();

            return new ServerCallContext(this, newRpc.Method, newRpc.Host, realtimeDeadline,
                newRpc.RequestMetadata, cancellationToken,
                ServerCallContext_WriteHeadersFunc, ServerCallContext_WriteOptionsGetter, ServerCallContext_WriteOptionsSetter,
                ServerCallContext_PeerGetter, ServerCallContext_AuthContextGetter, ServerCallContext_ContextPropagationTokenFactory);
        }

        private AuthContext GetAuthContextEager()
        {
            using (var authContextNative = callHandle.GetAuthContext())
            {
                return authContextNative.ToAuthContext();
            }
        }

        // Implementors of ServerCallContext's members are pre-allocated to avoid unneccessary delegate allocations.
        readonly static Func<ServerCallContext, object, Metadata, Task> ServerCallContext_WriteHeadersFunc = (ctx, extraData, headers) =>
        {
            return ((ServerCallContextExtraData)extraData).serverResponseStream.WriteResponseHeadersAsync(headers);
        };

        readonly static Func<ServerCallContext, object, WriteOptions> ServerCallContext_WriteOptionsGetter = (ctx, extraData) =>
        {

            return ((ServerCallContextExtraData)extraData).serverResponseStream.WriteOptions;
        };

        readonly static Action<ServerCallContext, object, WriteOptions> ServerCallContext_WriteOptionsSetter = (ctx, extraData, options) =>
        {
            ((ServerCallContextExtraData)extraData).serverResponseStream.WriteOptions = options;
        };

        readonly static Func<ServerCallContext, object, string> ServerCallContext_PeerGetter = (ctx, extraData) =>
        {
            // Getting the peer lazily is fine as the native call is guaranteed
            // not to be disposed before user-supplied server side handler returns.
            // Most users won't need to read this field anyway.
            return ((ServerCallContextExtraData)extraData).callHandle.GetPeer();
        };

        readonly static Func<ServerCallContext, object, AuthContext> ServerCallContext_AuthContextGetter = (ctx, extraData) =>
        {
            return ((ServerCallContextExtraData)extraData).cachedAuthContext.Value;
        };

        readonly static Func<ServerCallContext, object, ContextPropagationOptions, ContextPropagationToken> ServerCallContext_ContextPropagationTokenFactory = (ctx, extraData, options) =>
        {
            var callHandle = ((ServerCallContextExtraData)extraData).callHandle;
            return new ContextPropagationToken(callHandle, ctx.Deadline, ctx.CancellationToken, options);
        };
    }
}
