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
using Grpc.Core.Profiling;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class UserAgentStringTest
    {
        const string Host = "127.0.0.1";

        MockServiceHelper helper;
        Server server;
        Channel channel;

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void DefaultUserAgentString()
        {
            helper = new MockServiceHelper(Host);
            server = helper.GetServer();
            server.Start();
            channel = helper.GetChannel();

            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var userAgentString = context.RequestHeaders.First(m => (m.Key == "user-agent")).Value;
                var parts = userAgentString.Split(new [] {' '}, 2);
                Assert.AreEqual(string.Format("grpc-csharp/{0}", VersionInfo.CurrentVersion), parts[0]);
                Assert.IsTrue(parts[1].StartsWith("grpc-c/"));
                return Task.FromResult("PASS");
            });
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), ""));
        }

        [Test]
        public void ApplicationUserAgentString()
        {
            helper = new MockServiceHelper(Host,
                channelOptions: new[] { new ChannelOption(ChannelOptions.PrimaryUserAgentString, "XYZ") });
            server = helper.GetServer();
            server.Start();
            channel = helper.GetChannel();
            
            channel = helper.GetChannel();
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var userAgentString = context.RequestHeaders.First(m => (m.Key == "user-agent")).Value;
                var parts = userAgentString.Split(new[] { ' ' }, 3);
                Assert.AreEqual("XYZ", parts[0]);
                return Task.FromResult("PASS");
            });
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), ""));
        }
    }
}
