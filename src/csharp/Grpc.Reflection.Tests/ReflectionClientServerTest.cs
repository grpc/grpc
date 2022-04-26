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

        [OneTimeSetUp]
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

        [OneTimeTearDown]
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
