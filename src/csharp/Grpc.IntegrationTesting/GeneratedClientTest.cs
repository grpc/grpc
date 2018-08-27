#region Copyright notice and license

// Copyright 2015-2016 gRPC authors.
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
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class GeneratedClientTest
    {
        TestService.TestServiceClient unimplementedClient = new UnimplementedTestServiceClient();

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
