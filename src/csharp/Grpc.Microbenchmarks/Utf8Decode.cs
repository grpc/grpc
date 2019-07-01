using System;
using System.Collections.Generic;
using System.Text;
using BenchmarkDotNet.Attributes;
using Grpc.Core.Internal;

namespace Grpc.Microbenchmarks
{
    [ClrJob, CoreJob] // test .NET Core and .NET Framework
    [MemoryDiagnoser] // allocations
    public class Utf8Decode
    {
        [Params(0, 1, 4, 128, 1024)]
        public int PayloadSize { get; set; }

        static readonly Dictionary<int, byte[]> Payloads = new Dictionary<int, byte[]> {
            { 0, Invent(0) },
            { 1, Invent(1) },
            { 4, Invent(4) },
            { 128, Invent(128) },
            { 1024, Invent(1024) },
        };

        static byte[] Invent(int length)
        {
            var rand = new Random(Seed: length);
            var chars = new char[length];
            for(int i = 0; i < chars.Length; i++)
            {
                chars[i] = (char)rand.Next(32, 300);
            }
            return Encoding.UTF8.GetBytes(chars);
        }

        const int Iterations = 1000;
        [Benchmark(OperationsPerInvoke = Iterations)]
        public unsafe void Run()
        {
            byte[] payload = Payloads[PayloadSize];
            fixed (byte* ptr = payload)
            {
                var iPtr = new IntPtr(ptr);
                for (int i = 0; i < Iterations; i++)
                {
                    MarshalUtils.PtrToStringUTF8(iPtr, payload.Length);
                }
            }
        }
    }
}
