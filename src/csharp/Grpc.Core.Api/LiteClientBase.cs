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
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Base class for lightweight client-side stubs.
    /// All calls are invoked via a <c>CallInvoker</c>.
    /// Lite client stubs have no configuration knobs, all configuration
    /// is provided by decorating the call invoker.
    /// Note: experimental API that can change or be removed without any prior notice.
    /// </summary>
    public abstract class LiteClientBase
    {
        readonly CallInvoker callInvoker;

        /// <summary>
        /// Initializes a new instance of <c>LiteClientBase</c> class that
        /// throws <c>NotImplementedException</c> upon invocation of any RPC.
        /// This constructor is only provided to allow creation of test doubles
        /// for client classes (e.g. mocking requires a parameterless constructor).
        /// </summary>
        protected LiteClientBase() : this(new UnimplementedCallInvoker())
        {
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="callInvoker">The <c>CallInvoker</c> for remote call invocation.</param>
        public LiteClientBase(CallInvoker callInvoker)
        {
            this.callInvoker = GrpcPreconditions.CheckNotNull(callInvoker, nameof(callInvoker));
        }

        /// <summary>
        /// Gets the call invoker.
        /// </summary>
        protected CallInvoker CallInvoker
        {
            get { return this.callInvoker; }
        }

        /// <summary>
        /// Call invoker that throws <c>NotImplementedException</c> for all requests.
        /// </summary>
        private class UnimplementedCallInvoker : CallInvoker
        {
            public UnimplementedCallInvoker()
            {
            }

            public override TResponse BlockingUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
            {
                throw new NotImplementedException();
            }

            public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
            {
                throw new NotImplementedException();
            }

            public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options, TRequest request)
            {
                throw new NotImplementedException();
            }

            public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options)
            {
                throw new NotImplementedException();
            }

            public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(Method<TRequest, TResponse> method, string host, CallOptions options)
            {
                throw new NotImplementedException();
            }
        }
    }
}
