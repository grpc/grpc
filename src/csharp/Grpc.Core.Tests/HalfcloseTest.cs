#region Copyright notice and license

// Copyright 2016 gRPC authors.
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
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class HalfcloseTest
    {
        MockServiceHelper helper;
        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            helper = new MockServiceHelper();

            server = helper.GetServer();
            server.Start();
            channel = helper.GetChannel();
        }

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        /// <summary>
        /// For client streaming and duplex streaming calls, if server does a full close
        /// before we halfclose the request stream, an attempt to halfclose
        /// (complete the request stream) shouldn't be treated as an error.
        /// </summary>
        [Test]
        public async Task HalfcloseAfterFullclose_ClientStreamingCall()
        {
            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                return "PASS";
            });

            var call = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall());
            // make sure server has fullclosed on us
            Assert.AreEqual("PASS", await call.ResponseAsync);

            // sending close from client should be still fine because server can finish
            // the call anytime and we cannot do anything about it on the client side.
            await call.RequestStream.CompleteAsync();

            // Second attempt to close from client is not allowed.
            Assert.ThrowsAsync(typeof(InvalidOperationException), async () => await call.RequestStream.CompleteAsync());
        }
    }
}
