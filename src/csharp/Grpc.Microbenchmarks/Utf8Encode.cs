using System;
using System.Collections.Generic;
using System.Text;
using BenchmarkDotNet.Attributes;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Internal.Tests;

namespace Grpc.Microbenchmarks
{
    [ClrJob, CoreJob] // test .NET Core and .NET Framework
    [MemoryDiagnoser] // allocations
    public class Utf8Encode : ISendStatusFromServerCompletionCallback
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        [Params(0, 1, 4, 128, 1024)]
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

        [GlobalSetup]
        public void Setup()
        {
            Native.grpcsharp_test_override_method("grpcsharp_call_start_batch", "nop");
            metadata = MetadataArraySafeHandle.Create(Metadata.Empty);
            call = new FakeNativeCall();
        }

        public void Cleanup()
        {
            metadata.Dispose();
            metadata = null;
            call.Dispose();
            call = null;
        }
        private INativeCall call;
        private MetadataArraySafeHandle metadata;

        const int Iterations = 1000;
        [Benchmark(OperationsPerInvoke = Iterations)]
        public unsafe void Run()
        {
            string payload = Payloads[PayloadSize];
            var status = new Status(StatusCode.OK, payload);
            for (int i = 0; i < Iterations; i++)
            {
                call.StartSendStatusFromServer(this, status,
                    metadata, false, null, WriteFlags.NoCompress);
            }
        }

        void ISendStatusFromServerCompletionCallback.OnSendStatusFromServerCompletion(bool success) { }
    }
}
