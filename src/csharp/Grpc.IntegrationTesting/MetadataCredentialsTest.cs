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

        FakeTestService serviceImpl;
        Server server;
        Channel channel;
        TestService.TestServiceClient client;
        List<ChannelOption> options;

        [SetUp]
        public void Init()
        {
            serviceImpl = new FakeTestService();
            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { TestService.BindService(serviceImpl) },
                Ports = { { Host, ServerPort.PickUnused, TestCredentials.CreateSslServerCredentials() } }
            };
            server.Start();

            options = new List<ChannelOption>
            {
                new ChannelOption(ChannelOptions.SslTargetNameOverride, TestCredentials.DefaultHostOverride)
            };
        }

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void MetadataCredentials_Channel()
        {
            serviceImpl.UnaryCallHandler = (req, context) =>
            {
                var authToken = context.RequestHeaders.First((entry) => entry.Key == "authorization").Value;
                Assert.AreEqual("SECRET_TOKEN", authToken);
                return Task.FromResult(new SimpleResponse());
            };

            var asyncAuthInterceptor = new AsyncAuthInterceptor(async (context, metadata) =>
            {
                await Task.Delay(100).ConfigureAwait(false);  // make sure the operation is asynchronous.
                metadata.Add("authorization", "SECRET_TOKEN");
            });

            var channelCredentials = ChannelCredentials.Create(TestCredentials.CreateSslCredentials(),
                CallCredentials.FromInterceptor(asyncAuthInterceptor));
            channel = new Channel(Host, server.Ports.Single().BoundPort, channelCredentials, options);
            client = new TestService.TestServiceClient(channel);

            client.UnaryCall(new SimpleRequest { });
        }

        [Test]
        public void MetadataCredentials_PerCall()
        {
            serviceImpl.UnaryCallHandler = (req, context) =>
            {
                var authToken = context.RequestHeaders.First((entry) => entry.Key == "authorization").Value;
                Assert.AreEqual("SECRET_TOKEN", authToken);
                return Task.FromResult(new SimpleResponse());
            };

            var asyncAuthInterceptor = new AsyncAuthInterceptor(async (context, metadata) =>
            {
                await Task.Delay(100).ConfigureAwait(false);  // make sure the operation is asynchronous.
                metadata.Add("authorization", "SECRET_TOKEN");
            });

            channel = new Channel(Host, server.Ports.Single().BoundPort, TestCredentials.CreateSslCredentials(), options);
            client = new TestService.TestServiceClient(channel);

            var callCredentials = CallCredentials.FromInterceptor(asyncAuthInterceptor);
            client.UnaryCall(new SimpleRequest { }, new CallOptions(credentials: callCredentials));
        }

        [Test]
        public void MetadataCredentials_BothChannelAndPerCall()
        {
            serviceImpl.UnaryCallHandler = (req, context) =>
            {
                var firstAuth = context.RequestHeaders.First((entry) => entry.Key == "first_authorization").Value;
                Assert.AreEqual("FIRST_SECRET_TOKEN", firstAuth);
                var secondAuth = context.RequestHeaders.First((entry) => entry.Key == "second_authorization").Value;
                Assert.AreEqual("SECOND_SECRET_TOKEN", secondAuth);
                // both values of "duplicate_authorization" are sent
                Assert.AreEqual("value1", context.RequestHeaders.First((entry) => entry.Key == "duplicate_authorization").Value);
                Assert.AreEqual("value2", context.RequestHeaders.Last((entry) => entry.Key == "duplicate_authorization").Value);
                return Task.FromResult(new SimpleResponse());
            };

            var channelCallCredentials = CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) => {
                metadata.Add("first_authorization", "FIRST_SECRET_TOKEN");
                metadata.Add("duplicate_authorization", "value1");
                return TaskUtils.CompletedTask;
            }));
            var perCallCredentials = CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) => {
                metadata.Add("second_authorization", "SECOND_SECRET_TOKEN");
                metadata.Add("duplicate_authorization", "value2");
                return TaskUtils.CompletedTask;
            }));

            var channelCredentials = ChannelCredentials.Create(TestCredentials.CreateSslCredentials(), channelCallCredentials);
            channel = new Channel(Host, server.Ports.Single().BoundPort, channelCredentials, options);
            client = new TestService.TestServiceClient(channel);

            client.UnaryCall(new SimpleRequest { }, new CallOptions(credentials: perCallCredentials));
        }

        [Test]
        public async Task MetadataCredentials_Composed()
        {
            serviceImpl.StreamingOutputCallHandler = async (req, responseStream, context) =>
            {
                var firstAuth = context.RequestHeaders.Last((entry) => entry.Key == "first_authorization").Value;
                Assert.AreEqual("FIRST_SECRET_TOKEN", firstAuth);
                var secondAuth = context.RequestHeaders.First((entry) => entry.Key == "second_authorization").Value;
                Assert.AreEqual("SECOND_SECRET_TOKEN", secondAuth);
                var thirdAuth = context.RequestHeaders.First((entry) => entry.Key == "third_authorization").Value;
                Assert.AreEqual("THIRD_SECRET_TOKEN", thirdAuth);
                await responseStream.WriteAsync(new StreamingOutputCallResponse());
            };

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
            serviceImpl.StreamingOutputCallHandler = async (req, responseStream, context) =>
            {
                var firstAuth = context.RequestHeaders.Last((entry) => entry.Key == "first_authorization").Value;
                Assert.AreEqual("FIRST_SECRET_TOKEN", firstAuth);
                var secondAuth = context.RequestHeaders.First((entry) => entry.Key == "second_authorization").Value;
                Assert.AreEqual("SECOND_SECRET_TOKEN", secondAuth);
                await responseStream.WriteAsync(new StreamingOutputCallResponse());
            };

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
            var call = client.StreamingOutputCall(new StreamingOutputCallRequest{ },
                new CallOptions(credentials: CallCredentials.Compose(first, second)));
            Assert.IsTrue(await call.ResponseStream.MoveNext());
            Assert.IsFalse(await call.ResponseStream.MoveNext());
        }

        [Test]
        public void MetadataCredentials_InterceptorLeavesMetadataEmpty()
        {
            serviceImpl.UnaryCallHandler = (req, context) =>
            {
                var authHeaderCount = context.RequestHeaders.Count((entry) => entry.Key == "authorization");
                Assert.AreEqual(0, authHeaderCount);
                return Task.FromResult(new SimpleResponse());
            };
            var channelCredentials = ChannelCredentials.Create(TestCredentials.CreateSslCredentials(),
                CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) => TaskUtils.CompletedTask)));
            channel = new Channel(Host, server.Ports.Single().BoundPort, channelCredentials, options);
            client = new TestService.TestServiceClient(channel);
            client.UnaryCall(new SimpleRequest { });
        }

        [Test]
        public void MetadataCredentials_InterceptorThrows()
        {
            var authInterceptorExceptionMessage = "Auth interceptor throws";
            var callCredentials = CallCredentials.FromInterceptor(new AsyncAuthInterceptor((context, metadata) =>
            {
                throw new Exception(authInterceptorExceptionMessage);
            }));
            var channelCredentials = ChannelCredentials.Create(TestCredentials.CreateSslCredentials(), callCredentials);
            channel = new Channel(Host, server.Ports.Single().BoundPort, channelCredentials, options);
            client = new TestService.TestServiceClient(channel);

            var ex = Assert.Throws<RpcException>(() => client.UnaryCall(new SimpleRequest { }));
            Assert.AreEqual(StatusCode.Unavailable, ex.Status.StatusCode);
            StringAssert.Contains(authInterceptorExceptionMessage, ex.Status.Detail);
        }

        private class FakeTestService : TestService.TestServiceBase
        {
            public UnaryServerMethod<SimpleRequest, SimpleResponse> UnaryCallHandler;

            public ServerStreamingServerMethod<StreamingOutputCallRequest, StreamingOutputCallResponse> StreamingOutputCallHandler;

            public override Task<SimpleResponse> UnaryCall(SimpleRequest request, ServerCallContext context)
            {
                if (UnaryCallHandler != null)
                {
                    return UnaryCallHandler(request, context);
                }
                return base.UnaryCall(request, context);
            }

            public override Task StreamingOutputCall(StreamingOutputCallRequest request, IServerStreamWriter<StreamingOutputCallResponse> responseStream, ServerCallContext context)
            {
                if (StreamingOutputCallHandler != null)
                {
                    return StreamingOutputCallHandler(request, responseStream, context);
                }
                return base.StreamingOutputCall(request, responseStream, context);
            }
        }
    }
}
