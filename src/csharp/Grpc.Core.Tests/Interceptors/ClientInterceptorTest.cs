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
using System.Text;
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
                metadata = metadata ?? new Metadata();
                metadata.Add(new Metadata.Entry(HeaderKey, HeaderValue));
                return metadata;
            });
            Assert.AreEqual("PASS", callInvoker.BlockingUnaryCall(new Method<string, string>(MethodType.Unary, MockServiceHelper.ServiceName, "Unary", Marshallers.StringMarshaller, Marshallers.StringMarshaller), Host, new CallOptions(), ""));
        }

        [Test]
        public void CheckInterceptorOrderInClientInterceptors()
        {
            var helper = new MockServiceHelper(Host);
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult("PASS");
            });
            var server = helper.GetServer();
            server.Start();
            var stringBuilder = new StringBuilder();
            var callInvoker = helper.GetChannel().Intercept(metadata =>
            {
                metadata = metadata ?? new Metadata();
                stringBuilder.Append("interceptor1");
                return metadata;
            }).Intercept(metadata =>
            {
                metadata = metadata ?? new Metadata();
                stringBuilder.Append("interceptor2");
                return metadata;
            }).Intercept(metadata =>
            {
                metadata = metadata ?? new Metadata();
                stringBuilder.Append("interceptor3");
                return metadata;
            });
            Assert.AreEqual("PASS", callInvoker.BlockingUnaryCall(new Method<string, string>(MethodType.Unary, MockServiceHelper.ServiceName, "Unary", Marshallers.StringMarshaller, Marshallers.StringMarshaller), Host, new CallOptions(), ""));
            Assert.AreEqual("interceptor3interceptor2interceptor1", stringBuilder.ToString());
        }

        private class CountingInterceptor : GenericInterceptor
        {
            protected override ClientCallArbitrator<TRequest, TResponse> InterceptCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, bool clientStreaming, bool serverStreaming, TRequest request)
            {
                if (!clientStreaming)
                {
                    return null;
                }
                int counter = 0;
                return new ClientCallArbitrator<TRequest, TResponse>
                {
                    OnRequestMessage = m => { counter++; return m; },
                    OnUnaryResponse = x => (TResponse)(object)counter.ToString()  // Cast to object first is needed to satisfy the type-checker
                };
            }
        }

        [Test]
        public async Task CountNumberOfRequestsInClientInterceptors()
        {
            var helper = new MockServiceHelper(Host);
            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                string result = "";
                await requestStream.ForEachAsync((request) =>
                {
                    result += request;
                    return TaskUtils.CompletedTask;
                });
                await Task.Delay(100);
                return result;
            });

            var callInvoker = helper.GetChannel().Intercept(new CountingInterceptor());

            var server = helper.GetServer();
            server.Start();
            var call = callInvoker.AsyncClientStreamingCall(new Method<string, string>(MethodType.ClientStreaming, MockServiceHelper.ServiceName, "ClientStreaming", Marshallers.StringMarshaller, Marshallers.StringMarshaller), Host, new CallOptions());
            await call.RequestStream.WriteAllAsync(new string[] { "A", "B", "C" });
            Assert.AreEqual("3", await call.ResponseAsync);

            Assert.AreEqual(StatusCode.OK, call.GetStatus().StatusCode);
            Assert.IsNotNull(call.GetTrailers());
        }
    }
}
