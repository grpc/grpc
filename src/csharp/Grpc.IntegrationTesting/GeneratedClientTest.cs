#region Copyright notice and license

// Copyright 2015-2016, Google Inc.
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
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using Moq;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class GeneratedClientTest
    {
        TestService.TestServiceClient unimplementedClient = new UnimplementedTestServiceClient();

        [Test]
        public void ExpandedParamOverloadCanBeMocked()
        {
            var expected = new SimpleResponse();

            var mockClient = new Mock<TestService.TestServiceClient>();
            // mocking is relatively clumsy because one needs to specify value for all the optional params.
            mockClient.Setup(m => m.UnaryCall(It.IsAny<SimpleRequest>(), null, null, CancellationToken.None)).Returns(expected);

            Assert.AreSame(expected, mockClient.Object.UnaryCall(new SimpleRequest()));
        }

        [Test]
        public void CallOptionsOverloadCanBeMocked()
        {
            var expected = new SimpleResponse();

            var mockClient = new Mock<TestService.TestServiceClient>();
            mockClient.Setup(m => m.UnaryCall(It.IsAny<SimpleRequest>(), It.IsAny<CallOptions>())).Returns(expected);

            Assert.AreSame(expected, mockClient.Object.UnaryCall(new SimpleRequest(), new CallOptions()));
        }

        [Test]
        public void DefaultMethodStubThrows_UnaryCall()
        {
            Assert.Throws(typeof(NotImplementedException), () => unimplementedClient.UnaryCall(new SimpleRequest()));
        }

        [Test]
        public void DefaultMethodStubThrows_ClientStreaming()
        {
            Assert.Throws(typeof(NotImplementedException), () => unimplementedClient.StreamingInputCall());
        }

        [Test]
        public void DefaultMethodStubThrows_ServerStreaming()
        {
            Assert.Throws(typeof(NotImplementedException), () => unimplementedClient.StreamingOutputCall(new StreamingOutputCallRequest()));
        }

        [Test]
        public void DefaultMethodStubThrows_DuplexStreaming()
        {
            Assert.Throws(typeof(NotImplementedException), () => unimplementedClient.FullDuplexCall());
        }

        /// <summary>
        /// Subclass of the generated client that doesn't override any method stubs.
        /// </summary>
        private class UnimplementedTestServiceClient : TestService.TestServiceClient
        {
        }
    }
}
