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
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Reflection;
using Grpc.Reflection.V1Alpha;
using NUnit.Framework;

namespace Grpc.Reflection.Tests
{
    /// <summary>
    /// Reflection client talks to reflection server.
    /// </summary>
    public class ReflectionClientServerTest
    {
        const string Host = "localhost";
        Server server;
        Channel channel;
        ServerReflection.ServerReflectionClient client;
        ReflectionServiceImpl serviceImpl;

        [TestFixtureSetUp]
        public void Init()
        {
            serviceImpl = new ReflectionServiceImpl(ServerReflection.Descriptor);

            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { ServerReflection.BindService(serviceImpl) },
                Ports = { { Host, ServerPort.PickUnused, ServerCredentials.Insecure } }
            };
            server.Start();
            channel = new Channel(Host, server.Ports.Single().BoundPort, ChannelCredentials.Insecure);

            client = new ServerReflection.ServerReflectionClient(channel);
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public async Task FileByFilename_NotFound()
        {
            var response = await SingleRequestAsync(new ServerReflectionRequest
            {
                FileByFilename = "somepackage/nonexistent.proto"
            });
            Assert.AreEqual((int)StatusCode.NotFound, response.ErrorResponse.ErrorCode);
        }

        [Test]
        public async Task FileByFilename()
        {
            var response = await SingleRequestAsync(new ServerReflectionRequest
            {
                FileByFilename = "grpc/reflection/v1alpha/reflection.proto"
            });
            Assert.AreEqual(1, response.FileDescriptorResponse.FileDescriptorProto.Count);
            Assert.AreEqual(ReflectionReflection.Descriptor.SerializedData, response.FileDescriptorResponse.FileDescriptorProto[0]);
        }

        [Test]
        public async Task FileContainingSymbol()
        {
            var response = await SingleRequestAsync(new ServerReflectionRequest
            {
                FileContainingSymbol = "grpc.reflection.v1alpha.ServerReflection"
            });
            Assert.AreEqual(1, response.FileDescriptorResponse.FileDescriptorProto.Count);
            Assert.AreEqual(ReflectionReflection.Descriptor.SerializedData, response.FileDescriptorResponse.FileDescriptorProto[0]);
        }

        [Test]
        public async Task FileContainingSymbol_NotFound()
        {
            var response = await SingleRequestAsync(new ServerReflectionRequest
            {
                FileContainingSymbol = "somepackage.Nonexistent"
            });
            Assert.AreEqual((int)StatusCode.NotFound, response.ErrorResponse.ErrorCode);
        }

        [Test]
        public async Task ListServices()
        {
            var response = await SingleRequestAsync(new ServerReflectionRequest
            {
                ListServices = ""
            });
            Assert.AreEqual(1, response.ListServicesResponse.Service.Count);
            Assert.AreEqual(ServerReflection.Descriptor.FullName, response.ListServicesResponse.Service[0].Name);
        }

        [Test]
        public async Task FileContainingExtension()
        {
            var response = await SingleRequestAsync(new ServerReflectionRequest
            {
                FileContainingExtension = new ExtensionRequest()
            });
            Assert.AreEqual((int)StatusCode.Unimplemented, response.ErrorResponse.ErrorCode);
        }

        private async Task<ServerReflectionResponse> SingleRequestAsync(ServerReflectionRequest request)
        {
            var call = client.ServerReflectionInfo();
            await call.RequestStream.WriteAsync(request);
            Assert.IsTrue(await call.ResponseStream.MoveNext());

            var response = call.ResponseStream.Current;
            await call.RequestStream.CompleteAsync();
            Assert.IsFalse(await call.ResponseStream.MoveNext());
            return response;
        }
    }
}
