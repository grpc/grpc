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
using Grpc.Core.Internal;

namespace Grpc.Core
{
    // TODO: support adding timeout to methods.
    /// <summary>
    /// Base for client-side stubs.
    /// </summary>
    public abstract class AbstractStub<TStub, TConfig>
        where TConfig : StubConfiguration
    {
        readonly Channel channel;
        readonly TConfig config;

        public AbstractStub(Channel channel, TConfig config)
        {
            this.channel = channel;
            this.config = config;
        }

        public Channel Channel
        {
            get
            {
                return this.channel;
            }
        }

        /// <summary>
        /// Creates a new call to given method.
        /// </summary>
        protected Call<TRequest, TResponse> CreateCall<TRequest, TResponse>(string serviceName, Method<TRequest, TResponse> method)
        {
            var headerBuilder = Metadata.CreateBuilder();
            config.HeaderInterceptor(headerBuilder);
            return new Call<TRequest, TResponse>(serviceName, method, channel, headerBuilder.Build());
        }
    }
}
