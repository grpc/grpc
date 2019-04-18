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
using CommandLine;
using CommandLine.Text;

namespace Grpc.Microbenchmarks
{
    class Program
    {
        public enum MicrobenchmarkType
        {
            CompletionRegistry,
            PInvokeByteArray,
            SendMessage
        }

        private class BenchmarkOptions
        {
            [Option("benchmark", Required = true, HelpText = "Benchmark to run")]
            public MicrobenchmarkType Benchmark { get; set; }
        }

        public static void Main(string[] args)
        {
            GrpcEnvironment.SetLogger(new ConsoleLogger());
            var parserResult = Parser.Default.ParseArguments<BenchmarkOptions>(args)
                .WithNotParsed(errors => {
                    Console.WriteLine("Supported benchmarks:");
                    foreach (var enumValue in Enum.GetValues(typeof(MicrobenchmarkType)))
                    {
                        Console.WriteLine("  " + enumValue);
                    }
                    Environment.Exit(1);
                })
                .WithParsed(options =>
                {
                    switch (options.Benchmark)
                    {
                        case MicrobenchmarkType.CompletionRegistry:
                          RunCompletionRegistryBenchmark();
                          break;
                        case MicrobenchmarkType.PInvokeByteArray:
                          RunPInvokeByteArrayBenchmark();
                          break;
                        case MicrobenchmarkType.SendMessage:
                          RunSendMessageBenchmark();
                          break;
                        default:
                          throw new ArgumentException("Unsupported benchmark.");
                    }
                });
        }

        static void RunCompletionRegistryBenchmark()
        {
            var benchmark = new CompletionRegistryBenchmark();
            benchmark.Init();
            foreach (int threadCount in new int[] {1, 1, 2, 4, 8, 12})
            {
                foreach (bool useSharedRegistry in new bool[] {false, true})
                {
                    benchmark.Run(threadCount, 4 * 1000 * 1000, useSharedRegistry);
                }
            }
            benchmark.Cleanup();
        }

        static void RunPInvokeByteArrayBenchmark()
        {
            var benchmark = new PInvokeByteArrayBenchmark();
            benchmark.Init();
            foreach (int threadCount in new int[] {1, 1, 2, 4, 8, 12})
            {
                benchmark.Run(threadCount, 4 * 1000 * 1000, 0);
            }
            benchmark.Cleanup();
        }

        static void RunSendMessageBenchmark()
        {
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
