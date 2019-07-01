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
        [Params(0)] //, 1, 4, 128, 1024)]
        public int PayloadSize { get; set; }

        static readonly Dictionary<int, string> Payloads = new Dictionary<int, string> {
            { 0, Invent(0) },
            { 1, Invent(1) },
            { 4, Invent(4) },
            { 128, Invent(128) },
            { 1024, Invent(1024) },
        };

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

        [GlobalSetup]
        public void Setup()
        {
            var native = NativeMethods.Get();

            // ??? throws ???
            native.grpcsharp_test_override_method(nameof(NativeMethods.grpcsharp_call_send_status_from_server), "nop");

            environment = GrpcEnvironment.AddRef();
            metadata = MetadataArraySafeHandle.Create(Metadata.Empty);
            var completionRegistry = new CompletionRegistry(environment, () => environment.BatchContextPool.Lease(), () => throw new NotImplementedException());
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
            metadata?.Dispose();
            metadata = null;
            call?.Dispose();
            call = null;

            if (environment != null)
            {
                environment = null;
                GrpcEnvironment.ReleaseAsync().Wait();
            }
        }
        private CallSafeHandle call;
        private MetadataArraySafeHandle metadata;

        const int Iterations = 1000;
        [Benchmark(OperationsPerInvoke = Iterations)]
        public unsafe void Run()
        {
            string payload = Payloads[PayloadSize];
            var status = new Status(StatusCode.OK, payload);
            for (int i = 0; i < Iterations; i++)
            {
                call.StartSendStatusFromServer(this, status, metadata, false, null, WriteFlags.NoCompress);
            }
        }

        void ISendStatusFromServerCompletionCallback.OnSendStatusFromServerCompletion(bool success) { }
    }
}
