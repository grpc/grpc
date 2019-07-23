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
using Grpc.Core.Internal;
using System;

namespace Grpc.Microbenchmarks
{
    // this test measures the overhead of C# wrapping layer when invoking calls;
    // the marshallers **DO NOT ALLOCATE**, so any allocations
    // are from the framework, not the messages themselves

    [ClrJob, CoreJob] // test .NET Core and .NET Framework
    [MemoryDiagnoser] // allocations
    public class UnaryCallOverheadBenchmark
    {
        private static readonly Task<string> CompletedString = Task.FromResult("");
        private static readonly byte[] EmptyBlob = new byte[0];
        private static readonly Marshaller<string> EmptyMarshaller = new Marshaller<string>(_ => EmptyBlob, _ => "");
        private static readonly Method<string, string> PingMethod = new Method<string, string>(MethodType.Unary, nameof(PingBenchmark), "Ping", EmptyMarshaller, EmptyMarshaller);

        [Benchmark]
        public string SyncUnaryCallOverhead()
        {
            return client.Ping("", new CallOptions());
        }

        Channel channel;
        PingClient client;

        [GlobalSetup]
        public void Setup()
        {
            // create client, the channel will actually never connect because call logic will be short-circuited
            channel = new Channel("localhost", 10042, ChannelCredentials.Insecure);
            client = new PingClient(new DefaultCallInvoker(channel));

            var native = NativeMethods.Get();

            // replace the implementation of a native method with a fake
            NativeMethods.Delegates.grpcsharp_call_start_unary_delegate fakeCallStartUnary = (CallSafeHandle call, BatchContextSafeHandle ctx, byte[] sendBuffer, UIntPtr sendBufferLen, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags) => {
                return native.grpcsharp_test_call_start_unary_echo(call, ctx, sendBuffer, sendBufferLen, writeFlags, metadataArray, metadataFlags);
            };
            native.GetType().GetField(nameof(native.grpcsharp_call_start_unary)).SetValue(native, fakeCallStartUnary);

            NativeMethods.Delegates.grpcsharp_completion_queue_pluck_delegate fakeCqPluck = (CompletionQueueSafeHandle cq, IntPtr tag) => {
                return new CompletionQueueEvent {
                    type = CompletionQueueEvent.CompletionType.OpComplete,
                    success = 1,
                    tag = tag
                };
            };
            native.GetType().GetField(nameof(native.grpcsharp_completion_queue_pluck)).SetValue(native, fakeCqPluck);
        }

        [GlobalCleanup]
        public async Task Cleanup()
        {
            await channel.ShutdownAsync();
        }

        class PingClient : LiteClientBase
        {
            public PingClient(CallInvoker callInvoker) : base(callInvoker) { }

            public string Ping(string request, CallOptions options)
            {
                return CallInvoker.BlockingUnaryCall(PingMethod, null, options, request);
            }
        }
    }
}
