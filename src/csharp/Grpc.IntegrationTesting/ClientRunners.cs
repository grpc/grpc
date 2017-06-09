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
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Logging;
using Grpc.Core.Profiling;
using Grpc.Core.Utils;
using NUnit.Framework;
using Grpc.Testing;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Helper methods to start client runners for performance testing.
    /// </summary>
    public class ClientRunners
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<ClientRunners>();

        // Profilers to use for clients.
        static readonly BlockingCollection<BasicProfiler> profilers = new BlockingCollection<BasicProfiler>();

        internal static void AddProfiler(BasicProfiler profiler)
        {
            GrpcPreconditions.CheckNotNull(profiler);
            profilers.Add(profiler);
        }

        /// <summary>
        /// Creates a started client runner.
        /// </summary>
        public static IClientRunner CreateStarted(ClientConfig config)
        {
            Logger.Debug("ClientConfig: {0}", config);

            if (config.AsyncClientThreads != 0)
            {
                Logger.Warning("ClientConfig.AsyncClientThreads is not supported for C#. Ignoring the value");
            }
            if (config.CoreLimit != 0)
            {
                Logger.Warning("ClientConfig.CoreLimit is not supported for C#. Ignoring the value");
            }
            if (config.CoreList.Count > 0)
            {
                Logger.Warning("ClientConfig.CoreList is not supported for C#. Ignoring the value");
            }

            var channels = CreateChannels(config.ClientChannels, config.ServerTargets, config.SecurityParams);

            return new ClientRunnerImpl(channels,
                config.ClientType,
                config.RpcType,
                config.OutstandingRpcsPerChannel,
                config.LoadParams,
                config.PayloadConfig,
                config.HistogramParams,
                () => GetNextProfiler());
        }

        private static List<Channel> CreateChannels(int clientChannels, IEnumerable<string> serverTargets, SecurityParams securityParams)
        {
            GrpcPreconditions.CheckArgument(clientChannels > 0, "clientChannels needs to be at least 1.");
            GrpcPreconditions.CheckArgument(serverTargets.Count() > 0, "at least one serverTarget needs to be specified.");

            var credentials = securityParams != null ? TestCredentials.CreateSslCredentials() : ChannelCredentials.Insecure;
            List<ChannelOption> channelOptions = null;
            if (securityParams != null && securityParams.ServerHostOverride != "")
            {
                channelOptions = new List<ChannelOption>
                {
                    new ChannelOption(ChannelOptions.SslTargetNameOverride, securityParams.ServerHostOverride)
                };
            }

            var result = new List<Channel>();
            for (int i = 0; i < clientChannels; i++)
            {
                var target = serverTargets.ElementAt(i % serverTargets.Count());
                var channel = new Channel(target, credentials, channelOptions);
                result.Add(channel);
            }
            return result;
        }

        private static BasicProfiler GetNextProfiler()
        {
            BasicProfiler result = null;
            profilers.TryTake(out result);
            return result;
        }
    }

    internal class ClientRunnerImpl : IClientRunner
    {
        const double SecondsToNanos = 1e9;

        readonly List<Channel> channels;
        readonly ClientType clientType;
        readonly RpcType rpcType;
        readonly PayloadConfig payloadConfig;
        readonly Lazy<byte[]> cachedByteBufferRequest;
        readonly ThreadLocal<Histogram> threadLocalHistogram;

        readonly List<Task> runnerTasks;
        readonly CancellationTokenSource stoppedCts = new CancellationTokenSource();
        readonly WallClockStopwatch wallClockStopwatch = new WallClockStopwatch();
        readonly AtomicCounter statsResetCount = new AtomicCounter();
        
        public ClientRunnerImpl(List<Channel> channels, ClientType clientType, RpcType rpcType, int outstandingRpcsPerChannel, LoadParams loadParams, PayloadConfig payloadConfig, HistogramParams histogramParams, Func<BasicProfiler> profilerFactory)
        {
            GrpcPreconditions.CheckArgument(outstandingRpcsPerChannel > 0, "outstandingRpcsPerChannel");
            GrpcPreconditions.CheckNotNull(histogramParams, "histogramParams");
            this.channels = new List<Channel>(channels);
            this.clientType = clientType;
            this.rpcType = rpcType;
            this.payloadConfig = payloadConfig;
            this.cachedByteBufferRequest = new Lazy<byte[]>(() => new byte[payloadConfig.BytebufParams.ReqSize]);
            this.threadLocalHistogram = new ThreadLocal<Histogram>(() => new Histogram(histogramParams.Resolution, histogramParams.MaxPossible), true);

            this.runnerTasks = new List<Task>();
            foreach (var channel in this.channels)
            {
                for (int i = 0; i < outstandingRpcsPerChannel; i++)
                {
                    var timer = CreateTimer(loadParams, 1.0 / this.channels.Count / outstandingRpcsPerChannel);
                    var optionalProfiler = profilerFactory();
                    this.runnerTasks.Add(RunClientAsync(channel, timer, optionalProfiler));
                }
            }
        }

        public ClientStats GetStats(bool reset)
        {
            var histogramData = new HistogramData();
            foreach (var hist in threadLocalHistogram.Values)
            {
                hist.GetSnapshot(histogramData, reset);
            }

            var secondsElapsed = wallClockStopwatch.GetElapsedSnapshot(reset).TotalSeconds;

            if (reset)
            {
                statsResetCount.Increment();
            }

            GrpcEnvironment.Logger.Info("[ClientRunnerImpl.GetStats] GC collection counts: gen0 {0}, gen1 {1}, gen2 {2}, (histogram reset count:{3}, seconds since reset: {4})",
                GC.CollectionCount(0), GC.CollectionCount(1), GC.CollectionCount(2), statsResetCount.Count, secondsElapsed);

            // TODO: populate user time and system time
            return new ClientStats
            {
                Latencies = histogramData,
                TimeElapsed = secondsElapsed,
                TimeUser = 0,
                TimeSystem = 0
            };
        }

        public async Task StopAsync()
        {
            stoppedCts.Cancel();
            foreach (var runnerTask in runnerTasks)
            {
                await runnerTask;
            }
            foreach (var channel in channels)
            {
                await channel.ShutdownAsync();
            }
        }

        private void RunUnary(Channel channel, IInterarrivalTimer timer, BasicProfiler optionalProfiler)
        {
            if (optionalProfiler != null)
            {
                Profilers.SetForCurrentThread(optionalProfiler);
            }

            bool profilerReset = false;

            var client = new BenchmarkService.BenchmarkServiceClient(channel);
            var request = CreateSimpleRequest();
            var stopwatch = new Stopwatch();

            while (!stoppedCts.Token.IsCancellationRequested)
            {
                // after the first stats reset, also reset the profiler.
                if (optionalProfiler != null && !profilerReset && statsResetCount.Count > 0)
                {
                    optionalProfiler.Reset();
                    profilerReset = true;
                }

                stopwatch.Restart();
                client.UnaryCall(request);
                stopwatch.Stop();

                // spec requires data point in nanoseconds.
                threadLocalHistogram.Value.AddObservation(stopwatch.Elapsed.TotalSeconds * SecondsToNanos);

                timer.WaitForNext();
            }
        }

        private async Task RunUnaryAsync(Channel channel, IInterarrivalTimer timer)
        {
            var client = new BenchmarkService.BenchmarkServiceClient(channel);
            var request = CreateSimpleRequest();
            var stopwatch = new Stopwatch();

            while (!stoppedCts.Token.IsCancellationRequested)
            {
                stopwatch.Restart();
                await client.UnaryCallAsync(request);
                stopwatch.Stop();

                // spec requires data point in nanoseconds.
                threadLocalHistogram.Value.AddObservation(stopwatch.Elapsed.TotalSeconds * SecondsToNanos);

                await timer.WaitForNextAsync();
            }
        }

        private async Task RunStreamingPingPongAsync(Channel channel, IInterarrivalTimer timer)
        {
            var client = new BenchmarkService.BenchmarkServiceClient(channel);
            var request = CreateSimpleRequest();
            var stopwatch = new Stopwatch();

            using (var call = client.StreamingCall())
            {
                while (!stoppedCts.Token.IsCancellationRequested)
                {
                    stopwatch.Restart();
                    await call.RequestStream.WriteAsync(request);
                    await call.ResponseStream.MoveNext();
                    stopwatch.Stop();

                    // spec requires data point in nanoseconds.
                    threadLocalHistogram.Value.AddObservation(stopwatch.Elapsed.TotalSeconds * SecondsToNanos);

                    await timer.WaitForNextAsync();
                }

                // finish the streaming call
                await call.RequestStream.CompleteAsync();
                Assert.IsFalse(await call.ResponseStream.MoveNext());
            }
        }

        private async Task RunGenericStreamingAsync(Channel channel, IInterarrivalTimer timer)
        {
            var request = cachedByteBufferRequest.Value;
            var stopwatch = new Stopwatch();

            var callDetails = new CallInvocationDetails<byte[], byte[]>(channel, GenericService.StreamingCallMethod, new CallOptions());

            using (var call = Calls.AsyncDuplexStreamingCall(callDetails))
            {
                while (!stoppedCts.Token.IsCancellationRequested)
                {
                    stopwatch.Restart();
                    await call.RequestStream.WriteAsync(request);
                    await call.ResponseStream.MoveNext();
                    stopwatch.Stop();

                    // spec requires data point in nanoseconds.
                    threadLocalHistogram.Value.AddObservation(stopwatch.Elapsed.TotalSeconds * SecondsToNanos);

                    await timer.WaitForNextAsync();
                }

                // finish the streaming call
                await call.RequestStream.CompleteAsync();
                Assert.IsFalse(await call.ResponseStream.MoveNext());
            }
        }

        private Task RunClientAsync(Channel channel, IInterarrivalTimer timer, BasicProfiler optionalProfiler)
        {
            if (payloadConfig.PayloadCase == PayloadConfig.PayloadOneofCase.BytebufParams)
            {
                GrpcPreconditions.CheckArgument(clientType == ClientType.AsyncClient, "Generic client only supports async API");
                GrpcPreconditions.CheckArgument(rpcType == RpcType.Streaming, "Generic client only supports streaming calls");
                return RunGenericStreamingAsync(channel, timer);
            }

            GrpcPreconditions.CheckNotNull(payloadConfig.SimpleParams);
            if (clientType == ClientType.SyncClient)
            {
                GrpcPreconditions.CheckArgument(rpcType == RpcType.Unary, "Sync client can only be used for Unary calls in C#");
                // create a dedicated thread for the synchronous client
                return Task.Factory.StartNew(() => RunUnary(channel, timer, optionalProfiler), TaskCreationOptions.LongRunning);
            }
            else if (clientType == ClientType.AsyncClient)
            {
                switch (rpcType)
                {
                    case RpcType.Unary:
                        return RunUnaryAsync(channel, timer);
                    case RpcType.Streaming:
                        return RunStreamingPingPongAsync(channel, timer);
                }
            }
            throw new ArgumentException("Unsupported configuration.");
        }

        private SimpleRequest CreateSimpleRequest()
        {
            GrpcPreconditions.CheckNotNull(payloadConfig.SimpleParams);
            return new SimpleRequest
            {
                Payload = CreateZerosPayload(payloadConfig.SimpleParams.ReqSize),
                ResponseSize = payloadConfig.SimpleParams.RespSize
            };
        }

        private static Payload CreateZerosPayload(int size)
        {
            return new Payload { Body = ByteString.CopyFrom(new byte[size]) };
        }

        private static IInterarrivalTimer CreateTimer(LoadParams loadParams, double loadMultiplier)
        {
            switch (loadParams.LoadCase)
            {
                case LoadParams.LoadOneofCase.ClosedLoop:
                    return new ClosedLoopInterarrivalTimer();
                case LoadParams.LoadOneofCase.Poisson:
                    return new PoissonInterarrivalTimer(loadParams.Poisson.OfferedLoad * loadMultiplier);
                default:
                    throw new ArgumentException("Unknown load type");
            }
        }
    }
}
