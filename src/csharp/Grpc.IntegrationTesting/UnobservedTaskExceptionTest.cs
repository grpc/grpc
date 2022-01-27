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
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Runs interop tests in-process.
    /// </summary>
    public class UnobservedTaskExceptionTest
    {
        const string Host = "localhost";
        Server server;
        Channel channel;
        TestService.TestServiceClient client;

        [OneTimeSetUp]
        public void Init()
        {
            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { TestService.BindService(new TestServiceImpl()) },
                Ports = { { Host, ServerPort.PickUnused, ServerCredentials.Insecure } }
            };
            server.Start();

            int port = server.Ports.Single().BoundPort;
            channel = new Channel(Host, port, ChannelCredentials.Insecure);
            client = new TestService.TestServiceClient(channel);
        }

        [OneTimeTearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public async Task NoUnobservedTaskExceptionForAbandonedStreamingResponse()
        {
            // Verify that https://github.com/grpc/grpc/issues/17458 has been fixed.
            // Create a streaming response call, then cancel it without reading all the responses
            // and check that no unobserved task exceptions have been thrown.

            var unobservedTaskExceptionCounter = new AtomicCounter();

            TaskScheduler.UnobservedTaskException += (sender, e) => {
                unobservedTaskExceptionCounter.Increment();
                Console.WriteLine("Detected unobserved task exception: " + e.Exception);
            };

            var bodySizes = new List<int> { 10, 10, 10, 10, 10 };
            var request = new StreamingOutputCallRequest {
                ResponseParameters = { bodySizes.Select((size) => new ResponseParameters { Size = size }) }
            };

            for (int i = 0; i < 50; i++)
            {
                Console.WriteLine($"Starting iteration {i}");
                using (var call = client.StreamingOutputCall(request))
                {
                    // Intentionally only read the first response (we know there's more)
                    // The call will be cancelled as soon as we leave the "using" statement.
                    var firstResponse = await call.ResponseStream.MoveNext();
                }
                // Make it more likely to trigger the "Unobserved task exception" warning
                GC.Collect();
            }

            Assert.AreEqual(0, unobservedTaskExceptionCounter.Count);
        }
    }
}
