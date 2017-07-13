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
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Shows how to attach custom error details as a binary trailer.
    /// </summary>
    public class CustomErrorDetailsTest
    {
        const string DebugInfoTrailerName = "debug-info-bin";
        const string ExceptionDetail = "Exception thrown on purpose.";
        const string Host = "localhost";
        Server server;
        Channel channel;
        TestService.TestServiceClient client;

        [TestFixtureSetUp]
        public void Init()
        {
            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { TestService.BindService(new CustomErrorDetailsTestServiceImpl()) },
                Ports = { { Host, ServerPort.PickUnused, ServerCredentials.Insecure } }
            };
            server.Start();

            channel = new Channel(Host, server.Ports.Single().BoundPort, ChannelCredentials.Insecure);
            client = new TestService.TestServiceClient(channel);
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public async Task UnaryCall()
        {
            var call = client.UnaryCallAsync(new SimpleRequest { ResponseSize = 10 });

            try
            {
                await call.ResponseAsync;
                Assert.Fail();
            }
            catch (RpcException e)
            {
                Assert.AreEqual(StatusCode.Unknown, e.Status.StatusCode);
                var debugInfo = GetDebugInfo(call.GetTrailers());
                Assert.AreEqual(debugInfo.Detail, ExceptionDetail);
                Assert.IsNotEmpty(debugInfo.StackEntries);
            }
        }

        private DebugInfo GetDebugInfo(Metadata trailers)
        {
            var entry = trailers.First((e) => e.Key == DebugInfoTrailerName);
            return DebugInfo.Parser.ParseFrom(entry.ValueBytes);
        }

        private class CustomErrorDetailsTestServiceImpl : TestService.TestServiceBase
        {
            public override async Task<SimpleResponse> UnaryCall(SimpleRequest request, ServerCallContext context)
            {
                try
                {
                    throw new ArgumentException(ExceptionDetail);
                }
                catch (Exception e)
                {
                    // Fill debug info with some structured details about the failure.
                    var debugInfo = new DebugInfo();
                    debugInfo.Detail = e.Message;
                    debugInfo.StackEntries.AddRange(e.StackTrace.Split(new[] { Environment.NewLine }, StringSplitOptions.None)); 
                    context.ResponseTrailers.Add(DebugInfoTrailerName, debugInfo.ToByteArray());
                    throw new RpcException(new Status(StatusCode.Unknown, "The handler threw exception."));
                }
            }
        }
    }
}
