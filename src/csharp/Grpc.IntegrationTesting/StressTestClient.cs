#region Copyright notice and license

// Copyright 2015-2016, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#endregion

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

using CommandLine;
using CommandLine.Text;
using Grpc.Core;
using Grpc.Core.Logging;
using Grpc.Core.Utils;
using Grpc.Testing;

namespace Grpc.IntegrationTesting
{
    public class StressTestClient
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<StressTestClient>();
        const double SecondsToNanos = 1e9;

        private class ClientOptions
        {
            [Option("server_addresses", Default = "localhost:8080")]
            public string ServerAddresses { get; set; }

            [Option("test_cases", Default = "large_unary:100")]
            public string TestCases { get; set; }

            [Option("test_duration_secs", Default = -1)]
            public int TestDurationSecs { get; set; }

            [Option("num_channels_per_server", Default = 1)]
            public int NumChannelsPerServer { get; set; }

            [Option("num_stubs_per_channel", Default = 1)]
            public int NumStubsPerChannel { get; set; }

            [Option("metrics_port", Default = 8081)]
            public int MetricsPort { get; set; }
        }

        ClientOptions options;
        List<string> serverAddresses;
        Dictionary<string, int> weightedTestCases;
        WeightedRandomGenerator testCaseGenerator;

        // cancellation will be emitted once test_duration_secs has elapsed.
        CancellationTokenSource finishedTokenSource = new CancellationTokenSource();
        Histogram histogram = new Histogram(0.01, 60 * SecondsToNanos);

        private StressTestClient(ClientOptions options, List<string> serverAddresses, Dictionary<string, int> weightedTestCases)
        {
            this.options = options;
            this.serverAddresses = serverAddresses;
            this.weightedTestCases = weightedTestCases;
            this.testCaseGenerator = new WeightedRandomGenerator(this.weightedTestCases);
        }

        public static void Run(string[] args)
        {
            GrpcEnvironment.SetLogger(new ConsoleLogger());
            var parserResult = Parser.Default.ParseArguments<ClientOptions>(args)
                .WithNotParsed((x) => Environment.Exit(1))
                .WithParsed(options => {
                    GrpcPreconditions.CheckArgument(options.NumChannelsPerServer > 0);
                    GrpcPreconditions.CheckArgument(options.NumStubsPerChannel > 0);

                    var serverAddresses = options.ServerAddresses.Split(',');
                    GrpcPreconditions.CheckArgument(serverAddresses.Length > 0, "You need to provide at least one server address");

                    var testCases = ParseWeightedTestCases(options.TestCases);
                    GrpcPreconditions.CheckArgument(testCases.Count > 0, "You need to provide at least one test case");

                    var interopClient = new StressTestClient(options, serverAddresses.ToList(), testCases);
                    interopClient.Run().Wait();
                });
        }

        async Task Run()
        {
            var metricsServer = new Server()
            {
                Services = { MetricsService.BindService(new MetricsServiceImpl(histogram)) },
                Ports = { { "[::]", options.MetricsPort, ServerCredentials.Insecure } }
            };
            metricsServer.Start();

            if (options.TestDurationSecs >= 0)
            {
                finishedTokenSource.CancelAfter(TimeSpan.FromSeconds(options.TestDurationSecs));
            }

            var tasks = new List<Task>();
            var channels = new List<Channel>();
            foreach (var serverAddress in serverAddresses)
            {
                for (int i = 0; i < options.NumChannelsPerServer; i++)
                {
                    var channel = new Channel(serverAddress, ChannelCredentials.Insecure);
                    channels.Add(channel);
                    for (int j = 0; j < options.NumStubsPerChannel; j++)
                    {
                        var client = new TestService.TestServiceClient(channel);
                        var task = Task.Factory.StartNew(() => RunBodyAsync(client).GetAwaiter().GetResult(),
                            TaskCreationOptions.LongRunning);
                        tasks.Add(task);  
                    }
                }
            }
            await Task.WhenAll(tasks);

            foreach (var channel in channels)
            {
                await channel.ShutdownAsync();
            }

            await metricsServer.ShutdownAsync();
        }

        async Task RunBodyAsync(TestService.TestServiceClient client)
        {
            Logger.Info("Starting stress test client thread.");
            while (!finishedTokenSource.Token.IsCancellationRequested)
            {
                var testCase = testCaseGenerator.GetNext();

                var stopwatch = Stopwatch.StartNew();

                await RunTestCaseAsync(client, testCase);

                stopwatch.Stop();
                histogram.AddObservation(stopwatch.Elapsed.TotalSeconds * SecondsToNanos);
            }
            Logger.Info("Stress test client thread finished.");
        }

        async Task RunTestCaseAsync(TestService.TestServiceClient client, string testCase)
        {
            switch (testCase)
            {
                case "empty_unary":
                    InteropClient.RunEmptyUnary(client);
                    break;
                case "large_unary":
                    InteropClient.RunLargeUnary(client);
                    break;
                case "client_streaming":
                    await InteropClient.RunClientStreamingAsync(client);
                    break;
                case "server_streaming":
                    await InteropClient.RunServerStreamingAsync(client);
                    break;
                case "ping_pong":
                    await InteropClient.RunPingPongAsync(client);
                    break;
                case "empty_stream":
                    await InteropClient.RunEmptyStreamAsync(client);
                    break;
                case "cancel_after_begin":
                    await InteropClient.RunCancelAfterBeginAsync(client);
                    break;
                case "cancel_after_first_response":
                    await InteropClient.RunCancelAfterFirstResponseAsync(client);
                    break;
                case "timeout_on_sleeping_server":
                    await InteropClient.RunTimeoutOnSleepingServerAsync(client);
                    break;
                case "custom_metadata":
                    await InteropClient.RunCustomMetadataAsync(client);
                    break;
                case "status_code_and_message":
                    await InteropClient.RunStatusCodeAndMessageAsync(client);
                    break;
                default:
                    throw new ArgumentException("Unsupported test case  " + testCase);
            }
        }

        static Dictionary<string, int> ParseWeightedTestCases(string weightedTestCases)
        {
            var result = new Dictionary<string, int>();
            foreach (var weightedTestCase in weightedTestCases.Split(','))
            {
                var parts = weightedTestCase.Split(new char[] {':'}, 2);
                GrpcPreconditions.CheckArgument(parts.Length == 2, "Malformed test_cases option.");
                result.Add(parts[0], int.Parse(parts[1]));
            }
            return result;
        }

        class WeightedRandomGenerator
        {
            readonly Random random = new Random();
            readonly List<Tuple<int, string>> cumulativeSums;
            readonly int weightSum;

            public WeightedRandomGenerator(Dictionary<string, int> weightedItems)
            {
                cumulativeSums = new List<Tuple<int, string>>();
                weightSum = 0;
                foreach (var entry in weightedItems)
                {
                    weightSum += entry.Value;
                    cumulativeSums.Add(Tuple.Create(weightSum, entry.Key));
                }
            }

            public string GetNext()
            {
                int rand = random.Next(weightSum);
                foreach (var entry in cumulativeSums)
                {
                    if (rand < entry.Item1)
                    {
                        return entry.Item2;
                    }
                }
                throw new InvalidOperationException("GetNext() failed.");
            }
        }

        class MetricsServiceImpl : MetricsService.MetricsServiceBase 
        {
            const string GaugeName = "csharp_overall_qps";

            readonly Histogram histogram;
            readonly WallClockStopwatch wallClockStopwatch = new WallClockStopwatch();

            public MetricsServiceImpl(Histogram histogram)
            {
                this.histogram = histogram;
            }

            public override Task<GaugeResponse> GetGauge(GaugeRequest request, ServerCallContext context)
            {
                if (request.Name == GaugeName)
                {
                    long qps = GetQpsAndReset();

                    return Task.FromResult(new GaugeResponse
                    {
                        Name = GaugeName,
                        LongValue = qps
                    });
                }
                throw new RpcException(new Status(StatusCode.InvalidArgument, "Gauge does not exist"));
            }

            public override async Task GetAllGauges(EmptyMessage request, IServerStreamWriter<GaugeResponse> responseStream, ServerCallContext context)
            {
                long qps = GetQpsAndReset();

                var response = new GaugeResponse
                {
                    Name = GaugeName,
                    LongValue = qps
                };
                await responseStream.WriteAsync(response);
            }

            long GetQpsAndReset()
            {
                var snapshot = histogram.GetSnapshot(true);
                var elapsedSnapshot = wallClockStopwatch.GetElapsedSnapshot(true);

                return (long) (snapshot.Count / elapsedSnapshot.TotalSeconds);
            }
        }
    }
}
