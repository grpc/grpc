// Copyright 2020 The gRPC Authors
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

using System;
using System.Net;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.HealthCheck;
using Helloworld;
using Grpc.Health;
using Grpc.Health.V1;
using Grpc.Reflection;
using Grpc.Reflection.V1Alpha;

namespace GreeterServer
{
    class GreeterImpl : Greeter.GreeterBase
    {
        // Server side handler of the SayHello RPC
        public override Task<HelloReply> SayHello(HelloRequest request, ServerCallContext context)
        {
            String hostName = Dns.GetHostName();
            return Task.FromResult(new HelloReply { Message = $"Hello {request.Name} from {hostName}!"});
        }
    }

    class Program
    {
        const int Port = 50051;

        public static void Main(string[] args)
        {
            var serviceDescriptors = new [] {Greeter.Descriptor, Health.Descriptor, ServerReflection.Descriptor};
            var greeterImpl = new GreeterImpl();
            var healthServiceImpl = new HealthServiceImpl();
            var reflectionImpl = new ReflectionServiceImpl(serviceDescriptors);

            Server server = new Server
            {
                Services = { Greeter.BindService(greeterImpl), Health.BindService(healthServiceImpl), ServerReflection.BindService(reflectionImpl) },
                Ports = { new ServerPort("localhost", Port, ServerCredentials.Insecure) }
            };
            server.Start();

            // Mark all services as healthy.
            foreach (var serviceDescriptor in serviceDescriptors)
            {
                healthServiceImpl.SetStatus(serviceDescriptor.FullName, HealthCheckResponse.Types.ServingStatus.Serving);
            }
            // Mark overall server status as healthy.
            healthServiceImpl.SetStatus("", HealthCheckResponse.Types.ServingStatus.Serving);

            Console.WriteLine("Greeter server listening on port " + Port);
            Console.WriteLine("Press any key to stop the server...");
            Console.ReadKey();

            server.ShutdownAsync().Wait();
        }
    }
}
