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
using System.Runtime.InteropServices;
using BenchmarkDotNet.Attributes;
using Grpc.Core.Internal;

namespace Grpc.Microbenchmarks
{
    public class ScalabilityExampleBenchmark : CommonThreadedBase
    {
        protected override bool NeedsEnvironment => false;

        // An example of testing scalability of a method that scales perfectly.
        // This method provides a baseline for how well can CommonThreadedBase
        // measure scalability.
        const int Iterations = 50 * 1000 * 1000;  // High number to make the overhead of RunConcurrent negligible.
        [Benchmark(OperationsPerInvoke = Iterations)]
        public void PerfectScalingExample()
        {
            RunConcurrent(() => { RunBody(); });
        }

        private int RunBody()
        {
            int result = 0;
            for (int i = 0; i < Iterations; i++)
            {
                // perform some operation that is completely independent from
                // other threads and therefore should scale perfectly if given
                // a dedicated thread.
                for (int j = 0; j < 100; j++)
                {
                   result = result ^ i ^ j ;
                }
            }
            return result;
        }
    }
}
