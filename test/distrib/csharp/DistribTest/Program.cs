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
using System.Linq;
using System.Threading.Tasks;
using Grpc.Core;
using Helloworld;

namespace TestGrpcPackage
{
    class MainClass
    {
        public static void Main(string[] args)
        {
            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            Server server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { Greeter.BindService(new GreeterImpl()) },
                Ports = { new ServerPort("localhost", ServerPort.PickUnused, ServerCredentials.Insecure) }
            };
            server.Start();

            Channel channel = new Channel("localhost", server.Ports.Single().BoundPort, ChannelCredentials.Insecure);

            try
            {
                var client = new Greeter.GreeterClient(channel);
                String user = "you";

                var reply = client.SayHello(new HelloRequest { Name = user });
                Console.WriteLine("Greeting: " + reply.Message);
                Console.WriteLine("Success!");
            }
            finally
            {
                channel.ShutdownAsync().Wait();
                server.ShutdownAsync().Wait();
            }
        }

        // Test that codegen works well in case the .csproj has .proto files
        // of the same name, but under different directories (see #17672).
        // This method doesn't need to be used, it is enough to check that it builds.
        private static object CheckDuplicateProtoFilesAreOk()
        {
            return new DuplicateProto.MessageFromDuplicateProto();
        }
    }

    class GreeterImpl : Greeter.GreeterBase
    {
        // Server side handler of the SayHello RPC
        public override Task<HelloReply> SayHello(HelloRequest request, ServerCallContext context)
        {
            return Task.FromResult(new HelloReply { Message = "Hello " + request.Name });
        }
    }
}
