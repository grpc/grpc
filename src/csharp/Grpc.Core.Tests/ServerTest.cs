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
using System.Linq;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class ServerTest
    {
        [Test]
        public void StartAndShutdownServer()
        {
            Server server = new Server
            {
                Ports = { new ServerPort("localhost", ServerPort.PickUnused, ServerCredentials.Insecure) }
            };
            server.Start();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void StartAndKillServer()
        {
            Server server = new Server
            {
                Ports = { new ServerPort("localhost", ServerPort.PickUnused, ServerCredentials.Insecure) }
            };
            server.Start();
            server.KillAsync().Wait();
        }

        [Test]
        public void PickUnusedPort()
        {
            Server server = new Server
            {
                Ports = { new ServerPort("localhost", ServerPort.PickUnused, ServerCredentials.Insecure) }
            };

            var boundPort = server.Ports.Single();
            Assert.AreEqual(0, boundPort.Port);
            Assert.Greater(boundPort.BoundPort, 0);

            server.Start();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void CannotModifyAfterStarted()
        {
            Server server = new Server
            {
                Ports = { new ServerPort("localhost", ServerPort.PickUnused, ServerCredentials.Insecure) }
            };
            server.Start();
            Assert.Throws(typeof(InvalidOperationException), () => server.Ports.Add("localhost", 9999, ServerCredentials.Insecure));
            Assert.Throws(typeof(InvalidOperationException), () => server.Services.Add(ServerServiceDefinition.CreateBuilder().Build()));

            server.ShutdownAsync().Wait();
        }

        [Test]
        public void UnstartedServerCanBeShutdown()
        {
            var server = new Server();
            server.ShutdownAsync().Wait();
            Assert.Throws(typeof(InvalidOperationException), () => server.Start());
        }

        [Test]
        public void UnstartedServerDoesNotPreventShutdown()
        {
            // just create a server, don't start it, and make sure it doesn't prevent shutdown.
            var server = new Server();
        }
    }
}
