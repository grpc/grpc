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
        const string Host = "127.0.0.1";

        [Test]
        public void AddRequestHeaderInClientInterceptor()
        {
            const string HeaderKey = "x-client-interceptor";
            const string HeaderValue = "hello-world";
            var helper = new MockServiceHelper(Host);
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var interceptorHeader = context.RequestHeaders.Last(m => (m.Key == HeaderKey)).Value;
                Assert.AreEqual(interceptorHeader, HeaderValue);
                return Task.FromResult("PASS");
            });
            var server = helper.GetServer();
            server.Start();
            var callInvoker = helper.GetChannel().Intercept(metadata =>
            {
                metadata.Add(new Metadata.Entry(HeaderKey, HeaderValue));
                return metadata;
            });
            Assert.AreEqual("PASS", callInvoker.BlockingUnaryCall(new Method<string, string>(MethodType.Unary, MockServiceHelper.ServiceName, "Unary", Marshallers.StringMarshaller, Marshallers.StringMarshaller), Host, new CallOptions().WithHeaders(new Metadata()), ""));
        }
    }
}
