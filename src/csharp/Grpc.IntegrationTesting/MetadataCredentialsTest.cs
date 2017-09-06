#region Copyright notice and license

// Copyright 2015-2016 gRPC authors.
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
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class MetadataCredentialsTest
    {
        const string Host = "localhost";
        Server server;
        Channel channel;
        TestService.TestServiceClient client;
        List<ChannelOption> options;
        AsyncAuthInterceptor asyncAuthInterceptor;

        [SetUp]
        public void Init()
        {
            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { TestService.BindService(new FakeTestService()) },
                Ports = { { Host, ServerPort.PickUnused, TestCredentials.CreateSslServerCredentials() } }
            };
            server.Start();

            options = new List<ChannelOption>
            {
                new ChannelOption(ChannelOptions.SslTargetNameOverride, TestCredentials.DefaultHostOverride)
            };

            asyncAuthInterceptor = new AsyncAuthInterceptor(async (context, metadata) =>
            {
                await Task.Delay(100).ConfigureAwait(false);  // make sure the operation is asynchronous.
                metadata.Add("authorization", "SECRET_TOKEN");
            });
        }

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void MetadataCredentials()
        {
            var channelCredentials = ChannelCredentials.Create(TestCredentials.CreateSslCredentials(),
                CallCredentials.FromInterceptor(asyncAuthInterceptor));
            channel = new Channel(Host, server.Ports.Single().BoundPort, channelCredentials, options);
            client = new TestService.TestServiceClient(channel);

            client.UnaryCall(new SimpleRequest { });
        }

        [Test]
        public void MetadataCredentials_PerCall()
        {
            channel = new Channel(Host, server.Ports.Single().BoundPort, TestCredentials.CreateSslCredentials(), options);
            client = new TestService.TestServiceClient(channel);

            var callCredentials = CallCredentials.FromInterceptor(asyncAuthInterceptor);
            client.UnaryCall(new SimpleRequest { }, new CallOptions(credentials: callCredentials));
        }

        [Test]
        public async Task MetadataCredentials_Composed()
        {
            var first = CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) => {
                // Attempt to exercise the case where async callback is inlineable/synchronously-runnable.
                metadata.Add("first_authorization", "FIRST_SECRET_TOKEN");
                return TaskUtils.CompletedTask;
            }));
            var second = CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) => {
                metadata.Add("second_authorization", "SECOND_SECRET_TOKEN");
                return TaskUtils.CompletedTask;
            }));
            var third = CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) => {
                metadata.Add("third_authorization", "THIRD_SECRET_TOKEN");
                return TaskUtils.CompletedTask;
            }));
            var channelCredentials = ChannelCredentials.Create(TestCredentials.CreateSslCredentials(),
                CallCredentials.Compose(first, second, third));
            channel = new Channel(Host, server.Ports.Single().BoundPort, channelCredentials, options);
            var client = new TestService.TestServiceClient(channel);
            var call = client.StreamingOutputCall(new StreamingOutputCallRequest { });
            Assert.IsTrue(await call.ResponseStream.MoveNext());
            Assert.IsFalse(await call.ResponseStream.MoveNext());
        }

        [Test]
        public async Task MetadataCredentials_ComposedPerCall()
        {
            channel = new Channel(Host, server.Ports.Single().BoundPort, TestCredentials.CreateSslCredentials(), options);
            var client = new TestService.TestServiceClient(channel);
            var first = CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) => {
                metadata.Add("first_authorization", "FIRST_SECRET_TOKEN");
                return TaskUtils.CompletedTask;
            }));
            var second = CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) => {
                metadata.Add("second_authorization", "SECOND_SECRET_TOKEN");
                return TaskUtils.CompletedTask;
            }));
            var third = CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) => {
                metadata.Add("third_authorization", "THIRD_SECRET_TOKEN");
                return TaskUtils.CompletedTask;
            }));
            var call = client.StreamingOutputCall(new StreamingOutputCallRequest{ },
                new CallOptions(credentials: CallCredentials.Compose(first, second, third)));
            Assert.IsTrue(await call.ResponseStream.MoveNext());
            Assert.IsFalse(await call.ResponseStream.MoveNext());
        }

        [Test]
        public void MetadataCredentials_InterceptorLeavesMetadataEmpty()
        {
            var channelCredentials = ChannelCredentials.Create(TestCredentials.CreateSslCredentials(),
                CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) => TaskUtils.CompletedTask)));
            channel = new Channel(Host, server.Ports.Single().BoundPort, channelCredentials, options);
            client = new TestService.TestServiceClient(channel);

            var ex = Assert.Throws<RpcException>(() => client.UnaryCall(new SimpleRequest { }));
            // StatusCode.Unknown as the server-side handler throws an exception after not receiving the authorization header.
            Assert.AreEqual(StatusCode.Unknown, ex.Status.StatusCode);
        }

        [Test]
        public void MetadataCredentials_InterceptorThrows()
        {
            var callCredentials = CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) =>
            {
                throw new Exception("Auth interceptor throws");
            }));
            var channelCredentials = ChannelCredentials.Create(TestCredentials.CreateSslCredentials(), callCredentials);
            channel = new Channel(Host, server.Ports.Single().BoundPort, channelCredentials, options);
            client = new TestService.TestServiceClient(channel);

            var ex = Assert.Throws<RpcException>(() => client.UnaryCall(new SimpleRequest { }));
            Assert.AreEqual(StatusCode.Unauthenticated, ex.Status.StatusCode);
        }

        private class FakeTestService : TestService.TestServiceBase
        {
            public override Task<SimpleResponse> UnaryCall(SimpleRequest request, ServerCallContext context)
            {
                var authToken = context.RequestHeaders.First((entry) => entry.Key == "authorization").Value;
                Assert.AreEqual("SECRET_TOKEN", authToken);
                return Task.FromResult(new SimpleResponse());
            }

            public override async Task StreamingOutputCall(StreamingOutputCallRequest request, IServerStreamWriter<StreamingOutputCallResponse> responseStream, ServerCallContext context)
            {
                var first = context.RequestHeaders.First((entry) => entry.Key == "first_authorization").Value;
                Assert.AreEqual("FIRST_SECRET_TOKEN", first);
                var second = context.RequestHeaders.First((entry) => entry.Key == "second_authorization").Value;
                Assert.AreEqual("SECOND_SECRET_TOKEN", second);
                var third = context.RequestHeaders.First((entry) => entry.Key == "third_authorization").Value;
                Assert.AreEqual("THIRD_SECRET_TOKEN", third);
                await responseStream.WriteAsync(new StreamingOutputCallResponse());
            }
        }
    }
}
