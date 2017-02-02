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
using System.IO;
using System.Linq;
using Grpc.Core;
using Grpc.Core.Logging;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class GrpcEnvironmentTest
    {
        [Test]
        public void InitializeAndShutdownGrpcEnvironment()
        {
            var env = GrpcEnvironment.AddRef();
            Assert.IsTrue(env.CompletionQueues.Count > 0);
            for (int i = 0; i < env.CompletionQueues.Count; i++)
            {
                Assert.IsNotNull(env.CompletionQueues.ElementAt(i));
            }
            GrpcEnvironment.ReleaseAsync().Wait();
        }

        [Test]
        public void SubsequentInvocations()
        {
            var env1 = GrpcEnvironment.AddRef();
            var env2 = GrpcEnvironment.AddRef();
            Assert.AreSame(env1, env2);
            GrpcEnvironment.ReleaseAsync().Wait();
            GrpcEnvironment.ReleaseAsync().Wait();
        }

        [Test]
        public void InitializeAfterShutdown()
        {
            Assert.AreEqual(0, GrpcEnvironment.GetRefCount());

            var env1 = GrpcEnvironment.AddRef();
            GrpcEnvironment.ReleaseAsync().Wait();

            var env2 = GrpcEnvironment.AddRef();
            GrpcEnvironment.ReleaseAsync().Wait();

            Assert.AreNotSame(env1, env2);
        }

        [Test]
        public void ReleaseWithoutAddRef()
        {
            Assert.AreEqual(0, GrpcEnvironment.GetRefCount());
            Assert.ThrowsAsync(typeof(InvalidOperationException), async () => await GrpcEnvironment.ReleaseAsync());
        }

        [Test]
        public void GetCoreVersionString()
        {
            var coreVersion = GrpcEnvironment.GetCoreVersionString();
            var parts = coreVersion.Split('.');
            Assert.AreEqual(3, parts.Length);
        }

        [Test]
        public void ChangingTheGlobalLogger()
        {
            // Note this relies partially on NullLogger and TextWriterLoggerImplementations, 
            // in that their ForType methods return instances of their same types.
            GlobalLoggerProxy<Channel> globalLoggerProxyForChannel = new GlobalLoggerProxy<Channel>();
            GlobalLoggerProxy<Server> globalLoggerProxyForServer = new GlobalLoggerProxy<Server>();

            NullLogger nullLogger = new NullLogger();
            GrpcEnvironment.SetLogger(nullLogger);

            Assert.IsInstanceOf<NullLogger>(globalLoggerProxyForChannel.GetLogger());
            Assert.IsInstanceOf<NullLogger>(globalLoggerProxyForServer.GetLogger());

            TextWriterLogger textWriterLogger = new TextWriterLogger(new StringWriter());
            GrpcEnvironment.SetLogger(textWriterLogger);

            Assert.IsInstanceOf<TextWriterLogger>(globalLoggerProxyForChannel.GetLogger());
            Assert.IsInstanceOf<TextWriterLogger>(globalLoggerProxyForServer.GetLogger());
        }
    }
}
