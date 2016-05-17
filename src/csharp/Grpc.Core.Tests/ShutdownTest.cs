#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
