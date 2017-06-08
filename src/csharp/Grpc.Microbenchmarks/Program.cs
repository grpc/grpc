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
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Logging;

namespace Grpc.Microbenchmarks
{
    class Program
    {
        public static void Main(string[] args)
        {
            GrpcEnvironment.SetLogger(new TextWriterLogger(Console.Error));
            var benchmark = new SendMessageBenchmark();
            benchmark.Init();
            foreach (int threadCount in new int[] {1, 1, 2, 4, 8, 12})
            {
                benchmark.Run(threadCount, 4 * 1000 * 1000, 0);
            }
            benchmark.Cleanup();
        }
    }
}
