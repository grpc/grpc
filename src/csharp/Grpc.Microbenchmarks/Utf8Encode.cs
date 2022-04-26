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

using System;
using System.Collections.Generic;
using BenchmarkDotNet.Attributes;
using Grpc.Core;
using Grpc.Core.Internal;

namespace Grpc.Microbenchmarks
{
    [ClrJob, CoreJob] // test .NET Core and .NET Framework
    [MemoryDiagnoser] // allocations
    public class Utf8Encode : ISendStatusFromServerCompletionCallback
    {
        [Params(0, 1, 4, 128, 1024)]
        public int PayloadSize
        {
            get { return payloadSize; }
            set
            {
                payloadSize = value;
                status = new Status(StatusCode.OK, Invent(value));
            }
        }

        private int payloadSize;
        private Status status;

        static string Invent(int length)
        {
            var rand = new Random(Seed: length);
            var chars = new char[length];
            for(int i = 0; i < chars.Length; i++)
            {
                chars[i] = (char)rand.Next(32, 300);
            }
            return new string(chars);
        }

        private GrpcEnvironment environment;
        private CompletionRegistry completionRegistry;
        [GlobalSetup]
        public void Setup()
        {
            var native = NativeMethods.Get();

            // nop the native-call via reflection
            NativeMethods.Delegates.grpcsharp_call_send_status_from_server_delegate nop = (CallSafeHandle call, BatchContextSafeHandle ctx, StatusCode statusCode, IntPtr statusMessage, UIntPtr statusMessageLen, MetadataArraySafeHandle metadataArray, int sendEmptyInitialMetadata, SliceBufferSafeHandle optionalSendBuffer, WriteFlags writeFlags) => {
                completionRegistry.Extract(ctx.Handle).OnComplete(true); // drain the dictionary as we go
                return CallError.OK;
            };
            native.GetType().GetField(nameof(native.grpcsharp_call_send_status_from_server)).SetValue(native, nop);

            environment = GrpcEnvironment.AddRef();
            metadata = MetadataArraySafeHandle.Create(Metadata.Empty);
            completionRegistry = new CompletionRegistry(environment, () => environment.BatchContextPool.Lease(), () => throw new NotImplementedException());
            var cq = CompletionQueueSafeHandle.CreateAsync(completionRegistry);
            call = CreateFakeCall(cq);
        }

        private static CallSafeHandle CreateFakeCall(CompletionQueueSafeHandle cq)
        {
            var call = CallSafeHandle.CreateFake(new IntPtr(0xdead), cq);
            bool success = false;
            while (!success)
            {
                // avoid calling destroy on a nonexistent grpc_call pointer
                call.DangerousAddRef(ref success);
            }
            return call;
        }

        [GlobalCleanup]
        public void Cleanup()
        {
            try
            {
                metadata?.Dispose();
                metadata = null;
                call?.Dispose();
                call = null;

                if (environment != null)
                {
                    environment = null;
                    // cleanup seems... unreliable on CLR
                    // GrpcEnvironment.ReleaseAsync().Wait(1000);
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine(ex.Message);
            }
        }
        private CallSafeHandle call;
        private MetadataArraySafeHandle metadata;

        const int Iterations = 1000;
        [Benchmark(OperationsPerInvoke = Iterations)]
        public unsafe void SendStatus()
        {
            for (int i = 0; i < Iterations; i++)
            {
                call.StartSendStatusFromServer(this, status, metadata, false, SliceBufferSafeHandle.NullInstance, WriteFlags.NoCompress);
            }
        }

        void ISendStatusFromServerCompletionCallback.OnSendStatusFromServerCompletion(bool success) { }
    }
}
