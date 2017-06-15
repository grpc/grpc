#region Copyright notice and license

// Copyright 2015 gRPC authors.
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
    public class ShutdownTest
    {
        const string Host = "127.0.0.1";

        MockServiceHelper helper;
        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            helper = new MockServiceHelper(Host);
            server = helper.GetServer();
            server.Start();
            channel = helper.GetChannel();
        }

        [Test]
        public async Task AbandonedCall_ServerKillAsync()
        {
            var readyToShutdown = new TaskCompletionSource<object>();
            helper.DuplexStreamingHandler = new DuplexStreamingServerMethod<string, string>(async (requestStream, responseStream, context) =>
            {
                readyToShutdown.SetResult(null);
                await requestStream.ToListAsync();
            });

            var call = Calls.AsyncDuplexStreamingCall(helper.CreateDuplexStreamingCall());
            await readyToShutdown.Task;  // make sure handler is running

            await channel.ShutdownAsync();  // channel.ShutdownAsync() works even if there's a pending call.
            await server.KillAsync();  // server.ShutdownAsync() would hang waiting for the call to finish.
        }
    }
}
