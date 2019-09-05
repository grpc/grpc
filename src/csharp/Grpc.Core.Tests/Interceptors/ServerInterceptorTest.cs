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
using Grpc.Core.Tests;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Interceptors.Tests
{
    public class ServerInterceptorTest
    {
        const string Host = "127.0.0.1";

        [Test]
        public void AddRequestHeaderInServerInterceptor()
        {
            var helper = new MockServiceHelper(Host);
            const string MetadataKey = "x-interceptor";
            const string MetadataValue = "hello world";
            var interceptor = new ServerCallContextInterceptor(ctx => ctx.RequestHeaders.Add(new Metadata.Entry(MetadataKey, MetadataValue)));
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var interceptorHeader = context.RequestHeaders.Last(m => (m.Key == MetadataKey)).Value;
                Assert.AreEqual(interceptorHeader, MetadataValue);
                return Task.FromResult("PASS");
            });
            helper.ServiceDefinition = helper.ServiceDefinition.Intercept(interceptor);
            var server = helper.GetServer();
            server.Start();
            var channel = helper.GetChannel();
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), ""));
        }

        [Test]
        public void VerifyInterceptorOrdering()
        {
            var helper = new MockServiceHelper(Host);
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult("PASS");
            });
            var stringBuilder = new StringBuilder();
            helper.ServiceDefinition = helper.ServiceDefinition
                .Intercept(new ServerCallContextInterceptor(ctx => stringBuilder.Append("A")))
                .Intercept(new ServerCallContextInterceptor(ctx => stringBuilder.Append("B1")),
                    new ServerCallContextInterceptor(ctx => stringBuilder.Append("B2")),
                    new ServerCallContextInterceptor(ctx => stringBuilder.Append("B3")))
                .Intercept(new ServerCallContextInterceptor(ctx => stringBuilder.Append("C")));
            var server = helper.GetServer();
            server.Start();
            var channel = helper.GetChannel();
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), ""));
            Assert.AreEqual("CB1B2B3A", stringBuilder.ToString());
        }

        [Test]
        public void UserStateVisibleToAllInterceptors()
        {
            object key1 = new object();
            object value1 = new object();
            const string key2 = "Interceptor #2";
            const string value2 = "Important state";

            var interceptor1 = new ServerCallContextInterceptor(ctx => {
                // state starts off empty
                Assert.AreEqual(0, ctx.UserState.Count);

                ctx.UserState.Add(key1, value1);
            });

            var interceptor2 = new ServerCallContextInterceptor(ctx => {
                // second interceptor can see state set by the first
                bool found = ctx.UserState.TryGetValue(key1, out object storedValue1);
                Assert.IsTrue(found);
                Assert.AreEqual(value1, storedValue1);

                ctx.UserState.Add(key2, value2);
            });

            var helper = new MockServiceHelper(Host);
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) => {
                // call handler can see all the state
                bool found = context.UserState.TryGetValue(key1, out object storedValue1);
                Assert.IsTrue(found);
                Assert.AreEqual(value1, storedValue1);

                found = context.UserState.TryGetValue(key2, out object storedValue2);
                Assert.IsTrue(found);
                Assert.AreEqual(value2, storedValue2);

                return Task.FromResult("PASS");
            });
            helper.ServiceDefinition = helper.ServiceDefinition
                .Intercept(interceptor2)
                .Intercept(interceptor1);

            var server = helper.GetServer();
            server.Start();
            var channel = helper.GetChannel();
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), ""));
        }

        [Test]
        public void CheckNullInterceptorRegistrationFails()
        {
            var helper = new MockServiceHelper(Host);
            var sd = helper.ServiceDefinition;
            Assert.Throws<ArgumentNullException>(() => sd.Intercept(default(Interceptor)));
            Assert.Throws<ArgumentNullException>(() => sd.Intercept(new[]{default(Interceptor)}));
            Assert.Throws<ArgumentNullException>(() => sd.Intercept(new[]{new ServerCallContextInterceptor(ctx=>{}), null}));
            Assert.Throws<ArgumentNullException>(() => sd.Intercept(default(Interceptor[])));
        }

        private class ServerCallContextInterceptor : Interceptor
        {
            readonly Action<ServerCallContext> interceptor;

            public ServerCallContextInterceptor(Action<ServerCallContext> interceptor)
            {
                GrpcPreconditions.CheckNotNull(interceptor, nameof(interceptor));
                this.interceptor = interceptor;
            }

            public override Task<TResponse> UnaryServerHandler<TRequest, TResponse>(TRequest request, ServerCallContext context, UnaryServerMethod<TRequest, TResponse> continuation)
            {
                interceptor(context);
                return continuation(request, context);
            }

            public override Task<TResponse> ClientStreamingServerHandler<TRequest, TResponse>(IAsyncStreamReader<TRequest> requestStream, ServerCallContext context, ClientStreamingServerMethod<TRequest, TResponse> continuation)
            {
                interceptor(context);
                return continuation(requestStream, context);
            }

            public override Task ServerStreamingServerHandler<TRequest, TResponse>(TRequest request, IServerStreamWriter<TResponse> responseStream, ServerCallContext context, ServerStreamingServerMethod<TRequest, TResponse> continuation)
            {
                interceptor(context);
                return continuation(request, responseStream, context);
            }

            public override Task DuplexStreamingServerHandler<TRequest, TResponse>(IAsyncStreamReader<TRequest> requestStream, IServerStreamWriter<TResponse> responseStream, ServerCallContext context, DuplexStreamingServerMethod<TRequest, TResponse> continuation)
            {
                interceptor(context);
                return continuation(requestStream, responseStream, context);
            }
        }
    }
}
