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
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var userAgentString = context.RequestHeaders.First(m => (m.Key == "user-agent")).Value;
                var parts = userAgentString.Split(new [] {' '}, 2);
                Assert.AreEqual(string.Format("grpc-csharp/{0}", VersionInfo.CurrentVersion), parts[0]);
                Assert.IsTrue(parts[1].StartsWith("grpc-c/"));
                return Task.FromResult("PASS");
            });

            server = helper.GetServer();
            server.Start();
            channel = helper.GetChannel();

            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), ""));
        }

        [Test]
        public void ApplicationUserAgentString()
        {
            helper = new MockServiceHelper(Host,
                channelOptions: new[] { new ChannelOption(ChannelOptions.PrimaryUserAgentString, "XYZ") });
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                var userAgentString = context.RequestHeaders.First(m => (m.Key == "user-agent")).Value;
                var parts = userAgentString.Split(new[] { ' ' }, 3);
                Assert.AreEqual("XYZ", parts[0]);
                return Task.FromResult("PASS");
            });

            server = helper.GetServer();
            server.Start();
            channel = helper.GetChannel();

            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), ""));
        }
    }
}
