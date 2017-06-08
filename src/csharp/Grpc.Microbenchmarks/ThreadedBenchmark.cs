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
using System.Threading;
using Grpc.Core;
using Grpc.Core.Internal;
using System.Collections.Generic;
using System.Diagnostics;

namespace Grpc.Microbenchmarks
{
    public class ThreadedBenchmark
    {
        List<ThreadStart> runners;

        public ThreadedBenchmark(IEnumerable<ThreadStart> runners)
        {
            this.runners = new List<ThreadStart>(runners);
        }

        public ThreadedBenchmark(int threadCount, Action threadBody)
        {
            this.runners = new List<ThreadStart>();
            for (int i = 0; i < threadCount; i++)
            {
                this.runners.Add(new ThreadStart(() => threadBody()));
            }
        }
        
        public void Run()
        {
            Console.WriteLine("Running threads.");
            var threads = new List<Thread>();
            for (int i = 0; i < runners.Count; i++)
            {
                var thread = new Thread(runners[i]);
                thread.Start();
                threads.Add(thread);
            }

            foreach (var thread in threads)
            {
                thread.Join();
            }
            Console.WriteLine("All threads finished.");
        }
    }
}
