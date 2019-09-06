#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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

using System.Threading.Tasks;
using BenchmarkDotNet.Attributes;
using Grpc.Core;

namespace Grpc.Microbenchmarks
{
    // this test creates a real server and client, measuring the inherent inbuilt
    // platform overheads; the marshallers **DO NOT ALLOCATE**, so any allocations
    // are from the framework, not the messages themselves

    // important: allocs are not reliable on .NET Core until .NET Core 3, since
    // this test involves multiple threads

    [ClrJob, CoreJob] // test .NET Core and .NET Framework
    [MemoryDiagnoser] // allocations
    public class PingBenchmark
    {
        private static readonly Task<string> CompletedString = Task.FromResult("");
        private static readonly byte[] EmptyBlob = new byte[0];
        private static readonly Marshaller<string> EmptyMarshaller = new Marshaller<string>(_ => EmptyBlob, _ => "");
        private static readonly Method<string, string> PingMethod = new Method<string, string>(MethodType.Unary, nameof(PingBenchmark), "Ping", EmptyMarshaller, EmptyMarshaller);


        [Benchmark]
        public async ValueTask<string> PingAsync()
        {
            using (var result = client.PingAsync("", new CallOptions()))
            {
                return await result.ResponseAsync;
            }
        }

        [Benchmark]
        public string Ping()
        {
            return client.Ping("", new CallOptions());
        }

        private Task<string> ServerMethod(string request, ServerCallContext context)
        {
            return CompletedString;
        }

        Server server;
        Channel channel;
        PingClient client;

        [GlobalSetup]
        public async Task Setup()
        {
            // create server
            server = new Server {
                Ports = { new ServerPort("localhost", 10042, ServerCredentials.Insecure) },
                Services = { ServerServiceDefinition.CreateBuilder().AddMethod(PingMethod, ServerMethod).Build() },
            };
            server.Start();

            // create client
            channel = new Channel("localhost", 10042, ChannelCredentials.Insecure);
            await channel.ConnectAsync();
            client = new PingClient(new DefaultCallInvoker(channel));
        }

        [GlobalCleanup]
        public async Task Cleanup()
        {
            await channel.ShutdownAsync();
            await server.ShutdownAsync();
        }

        class PingClient : ClientBase
        {
            public PingClient(CallInvoker callInvoker) : base(callInvoker) { }
            public AsyncUnaryCall<string> PingAsync(string request, CallOptions options)
            {
                return CallInvoker.AsyncUnaryCall(PingMethod, null, options, request);
            }
            public string Ping(string request, CallOptions options)
            {
                return CallInvoker.BlockingUnaryCall(PingMethod, null, options, request);
            }
        }
    }
}
