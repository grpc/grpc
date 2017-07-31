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
using NUnit.Framework;
using System.Threading;
using System.Threading.Tasks;

namespace Grpc.Core.Tests
{
    public class ThreadingModelTest
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

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void BlockingCallInServerHandlerDoesNotDeadlock()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                int recursionDepth = int.Parse(request);
                if (recursionDepth <= 0) {
                    return "SUCCESS";
                }
                return Calls.BlockingUnaryCall(helper.CreateUnaryCall(), (recursionDepth - 1).ToString());
            });

            int maxRecursionDepth = Environment.ProcessorCount * 2;  // make sure we have more pending blocking calls than threads in GrpcThreadPool
            Assert.AreEqual("SUCCESS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), maxRecursionDepth.ToString()));
        }

        [Test]
        public void HandlerDoesNotRunOnGrpcThread()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                if (IsRunningOnGrpcThreadPool()) {
                    return "Server handler should not run on gRPC threadpool thread.";
                }
                return request;
            });

            Assert.AreEqual("ABC", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "ABC"));
        }

        [Test]
        public async Task ContinuationDoesNotRunOnGrpcThread()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                return request;
            });

            await Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "ABC");
            Assert.IsFalse(IsRunningOnGrpcThreadPool());
        }

        private static bool IsRunningOnGrpcThreadPool()
        {
            var threadName = Thread.CurrentThread.Name ?? "";
            return threadName.Contains("grpc");
        }
    }
}
