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
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading.Tasks;

namespace Grpc.Core.Utils
{
    /// <summary>
    /// Utility methods to run microbenchmarks.
    /// </summary>
    public static class BenchmarkUtil
    {
        /// <summary>
        /// Runs a simple benchmark preceded by warmup phase.
        /// </summary>
        public static void RunBenchmark(int warmupIterations, int benchmarkIterations, Action action)
        {
            var logger = GrpcEnvironment.Logger;
            
            logger.Info("Warmup iterations: {0}", warmupIterations);
            for (int i = 0; i < warmupIterations; i++)
            {
                action();
            }

            logger.Info("Benchmark iterations: {0}", benchmarkIterations);
            var stopwatch = new Stopwatch();
            stopwatch.Start();
            for (int i = 0; i < benchmarkIterations; i++)
            {
                action();
            }
            stopwatch.Stop();
            logger.Info("Elapsed time: {0}ms", stopwatch.ElapsedMilliseconds);
            logger.Info("Ops per second: {0}", (int)((double)benchmarkIterations  * 1000 / stopwatch.ElapsedMilliseconds));
        }
    }
}
