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
using System.Threading.Tasks;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class AppDomainUnloadTest
    {
#if NETCOREAPP1_0
        [Test]
        [Ignore("Not supported for CoreCLR")]
        public void AppDomainUnloadHookCanCleanupAbandonedCall()
        {
        }
#else
        [Test]
        public void AppDomainUnloadHookCanCleanupAbandonedCall()
        {
            var setup = new AppDomainSetup
            {
                ApplicationBase = AppDomain.CurrentDomain.BaseDirectory
            };
            var childDomain = AppDomain.CreateDomain("test", null, setup);
            var remoteObj = childDomain.CreateInstance(typeof(AppDomainTestClass).Assembly.GetName().Name, typeof(AppDomainTestClass).FullName);

            // Try to unload the appdomain once we've created a server and a channel inside the appdomain.
            AppDomain.Unload(childDomain);
        }

        public class AppDomainTestClass
        {
            const string Host = "127.0.0.1";

            /// <summary>
            /// Creates a server and a channel and initiates a call. The code is invoked from inside of an AppDomain
            /// to test if AppDomain.Unload() work if Grpc is being used.
            /// </summary>
            public AppDomainTestClass()
            {
                var helper = new MockServiceHelper(Host);
                var readyToShutdown = new TaskCompletionSource<object>();
                helper.DuplexStreamingHandler = new DuplexStreamingServerMethod<string, string>(async (requestStream, responseStream, context) =>
                {
                    readyToShutdown.SetResult(null);
                    await requestStream.ToListAsync();
                });

                var server = helper.GetServer();
                server.Start();
                var channel = helper.GetChannel();

                var call = Calls.AsyncDuplexStreamingCall(helper.CreateDuplexStreamingCall());
                readyToShutdown.Task.Wait();  // make sure handler is running
            }
        }
#endif
    }
}
