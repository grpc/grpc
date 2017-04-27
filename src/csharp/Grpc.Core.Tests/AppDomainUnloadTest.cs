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
