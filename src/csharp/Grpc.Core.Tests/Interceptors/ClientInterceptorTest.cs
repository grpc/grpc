#region Copyright notice and license

// Copyright 2018 gRPC authors.
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
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Interceptors;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using Grpc.Core.Tests;
using NUnit.Framework;

namespace Grpc.Core.Interceptors.Tests
{
    public class ClientInterceptorTest
    {
        private class AddHeaderClientInterceptor : Interceptor
        {
            readonly Metadata.Entry header;
            public AddHeaderClientInterceptor(string key, string value)
            {
                this.header = new Metadata.Entry(key, value);
            }
            public override TResponse BlockingUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, BlockingUnaryCallContinuation<TRequest, TResponse> continuation)
            {
                context.Options.Headers.Add(this.header);
                return continuation(request, context);
            }

            public Metadata.Entry Header
            {
                get
                {
                    return this.header;
                }
            }
        }

        const string Host = "127.0.0.1";

        [Test]
        public void AddRequestHeaderInClientInterceptor()
        {
            var helper = new MockServiceHelper(Host);
            var interceptor = new AddHeaderClientInterceptor("x-client-interceptor", "hello world");
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var interceptorHeader = context.RequestHeaders.Last(m => (m.Key == interceptor.Header.Key)).Value;
                Assert.AreEqual(interceptorHeader, interceptor.Header.Value);
                return Task.FromResult("PASS");
            });
            var server = helper.GetServer();
            server.Start();
            var callInvoker = helper.GetChannel().Intercept(interceptor);
            Assert.AreEqual("PASS", callInvoker.BlockingUnaryCall(new Method<string, string>(MethodType.Unary, MockServiceHelper.ServiceName, "Unary", Marshallers.StringMarshaller, Marshallers.StringMarshaller), Host, new CallOptions().WithHeaders(new Metadata()), ""));
        }
    }
}
