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
        private static readonly Marshaller<byte[]> IdentityMarshaller = new Marshaller<byte[]>(msg => msg, payload => payload);
        private static readonly Method<byte[], byte[]> PingMethod = new Method<byte[], byte[]>(MethodType.Unary, nameof(PingBenchmark), "Ping", IdentityMarshaller, IdentityMarshaller);

        private int payloadSize;
        private byte[] payload;

        // size of payload that is sent as request and received as response.
        [Params(0, 1, 10, 100, 1000)]
        public int PayloadSize
        {
            get { return payloadSize; }
            set
            {
                payloadSize = value;
                payload = new byte[value];
            }
        }

        [Benchmark]
        public byte[] SyncUnaryCallOverhead()
        {
            return client.Ping(payload, new CallOptions());
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
            NativeMethods.Delegates.grpcsharp_call_start_unary_delegate fakeCallStartUnary = (CallSafeHandle call, BatchContextSafeHandle ctx, SliceBufferSafeHandle sendBuffer, WriteFlags writeFlags, MetadataArraySafeHandle metadataArray, CallFlags metadataFlags) => {
                return native.grpcsharp_test_call_start_unary_echo(call, ctx, sendBuffer, writeFlags, metadataArray, metadataFlags);
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

        class PingClient : ClientBase
        {
            public PingClient(CallInvoker callInvoker) : base(callInvoker) { }

            public byte[] Ping(byte[] request, CallOptions options)
            {
                return CallInvoker.BlockingUnaryCall(PingMethod, null, options, request);
            }
        }
    }
}
