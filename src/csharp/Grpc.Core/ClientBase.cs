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
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Generic base class for client-side stubs.
    /// </summary>
    public abstract class ClientBase<T> : ClientBase
        where T : ClientBase<T>
    {
        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class that
        /// throws <c>NotImplementedException</c> upon invocation of any RPC.
        /// This constructor is only provided to allow creation of test doubles
        /// for client classes (e.g. mocking requires a parameterless constructor).
        /// </summary>
        protected ClientBase() : base()
        {
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="channel">The channel to use for remote call invocation.</param>
        public ClientBase(Channel channel) : base(channel)
        {
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="callInvoker">The <c>CallInvoker</c> for remote call invocation.</param>
        public ClientBase(CallInvoker callInvoker) : base(callInvoker)
        {
        }

        /// <summary>
        /// Creates a new client that sets host field for calls explicitly.
        /// gRPC supports multiple "hosts" being served by a single server.
        /// By default (if a client was not created by calling this method),
        /// host <c>null</c> with the meaning "use default host" is used.
        /// </summary>
        public T WithHost(string host)
        {
            GrpcPreconditions.CheckNotNull(host, "host");
            var decoratedInvoker = new InterceptingCallInvoker(CallInvoker, hostInterceptor: (h) => host);
            return NewInstance(decoratedInvoker);
        }

        /// <summary>
        /// Creates a new instance of client from given <c>CallInvoker</c>.
        /// </summary>
        protected abstract T NewInstance(CallInvoker callInvoker);
    }

    /// <summary>
    /// Base class for client-side stubs.
    /// </summary>
    public abstract class ClientBase
    {
        readonly CallInvoker callInvoker;

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class that
        /// throws <c>NotImplementedException</c> upon invocation of any RPC.
        /// This constructor is only provided to allow creation of test doubles
        /// for client classes (e.g. mocking requires a parameterless constructor).
        /// </summary>
        protected ClientBase() : this(new UnimplementedCallInvoker())
        {
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="channel">The channel to use for remote call invocation.</param>
        public ClientBase(Channel channel) : this(new DefaultCallInvoker(channel))
        {
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="callInvoker">The <c>CallInvoker</c> for remote call invocation.</param>
        public ClientBase(CallInvoker callInvoker)
        {
            this.callInvoker = GrpcPreconditions.CheckNotNull(callInvoker);
        }

        /// <summary>
        /// Gets the call invoker.
        /// </summary>
        protected CallInvoker CallInvoker
        {
            get { return this.callInvoker; }
        }
    }
}
