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
    public class PerformanceTest
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
        [Category("Performance")]
        [Ignore("Prevent running on Jenkins")]
        public void UnaryCallPerformance()
        {
            var profiler = new BasicProfiler();
            Profilers.SetForCurrentThread(profiler);

            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                return request;
            });

            var callDetails = helper.CreateUnaryCall();
            for(int i = 0; i < 3000; i++)
            {
                Calls.BlockingUnaryCall(callDetails, "ABC");
            }

            profiler.Reset();

            for(int i = 0; i < 3000; i++)
            {
                Calls.BlockingUnaryCall(callDetails, "ABC");
            }
            profiler.Dump("latency_trace_csharp.txt");
        }
    }
}
