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
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class ChannelTest
    {
        [TestFixtureTearDown]
        public void CleanupClass()
        {
            GrpcEnvironment.Shutdown();
        }

        [Test]
        public void Constructor_RejectsInvalidParams()
        {
            Assert.Throws(typeof(ArgumentNullException), () => new Channel(null, SecurityOptions.Insecure));
        }

        [Test]
        public void State_IdleAfterCreation()
        {
            using (var channel = new Channel("localhost", SecurityOptions.Insecure))
            {
                Assert.AreEqual(ChannelState.Idle, channel.State);
            }
        }

        [Test]
        public void WaitForStateChangedAsync_InvalidArgument()
        {
            using (var channel = new Channel("localhost", SecurityOptions.Insecure))
            {
                Assert.Throws(typeof(ArgumentException), () => channel.WaitForStateChangedAsync(ChannelState.FatalFailure));
            }
        }

        [Test]
        public void ResolvedTarget()
        {
            using (var channel = new Channel("127.0.0.1", SecurityOptions.Insecure))
            {
                Assert.IsTrue(channel.ResolvedTarget.Contains("127.0.0.1"));
            }
        }

        [Test]
        public void Dispose_IsIdempotent()
        {
            var channel = new Channel("localhost", SecurityOptions.Insecure);
            channel.Dispose();
            channel.Dispose();
        }
    }
}
