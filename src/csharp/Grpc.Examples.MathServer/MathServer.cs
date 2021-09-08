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
using Grpc.Core;

namespace Math
{
    class MainClass
    {
        const string Host = "0.0.0.0";
        const int Port = 23456;

        public static async Task Main(string[] args)
        {
            Server server = new Server
            {
                Services = { Math.BindService(new MathServiceImpl()) },
                Ports = { { Host, Port, ServerCredentials.Insecure } }
            };
            server.Start();

            Console.WriteLine("MathServer listening on port " + Port);

            Console.WriteLine("Press any key to stop the server...");
            Console.ReadKey();

            await server.ShutdownAsync();
        }
    }
}
