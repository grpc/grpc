#region Copyright notice and license

// Copyright 2015, Google Inc.
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
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Logging;
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
                config.HistogramParams);
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
    }

    public class ClientRunnerImpl : IClientRunner
    {
        const double SecondsToNanos = 1e9;

        readonly List<Channel> channels;
        readonly ClientType clientType;
        readonly RpcType rpcType;
        readonly PayloadConfig payloadConfig;
        readonly Histogram histogram;

        readonly List<Task> runnerTasks;
        readonly CancellationTokenSource stoppedCts = new CancellationTokenSource();
        readonly WallClockStopwatch wallClockStopwatch = new WallClockStopwatch();
        
        public ClientRunnerImpl(List<Channel> channels, ClientType clientType, RpcType rpcType, int outstandingRpcsPerChannel, LoadParams loadParams, PayloadConfig payloadConfig, HistogramParams histogramParams)
        {
            GrpcPreconditions.CheckArgument(outstandingRpcsPerChannel > 0, "outstandingRpcsPerChannel");
            GrpcPreconditions.CheckNotNull(histogramParams, "histogramParams");
            this.channels = new List<Channel>(channels);
            this.clientType = clientType;
            this.rpcType = rpcType;
            this.payloadConfig = payloadConfig;
            this.histogram = new Histogram(histogramParams.Resolution, histogramParams.MaxPossible);

            this.runnerTasks = new List<Task>();
            foreach (var channel in this.channels)
            {
                for (int i = 0; i < outstandingRpcsPerChannel; i++)
                {
                    var timer = CreateTimer(loadParams, 1.0 / this.channels.Count / outstandingRpcsPerChannel);
                    var threadBody = GetThreadBody(channel, timer);
                    this.runnerTasks.Add(Task.Factory.StartNew(threadBody, TaskCreationOptions.LongRunning));
                }
            }
        }

        public ClientStats GetStats(bool reset)
        {
            var histogramData = histogram.GetSnapshot(reset);
            var secondsElapsed = wallClockStopwatch.GetElapsedSnapshot(reset).TotalSeconds;

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

        private void RunUnary(Channel channel, IInterarrivalTimer timer)
        {
            var client = BenchmarkService.NewClient(channel);
            var request = CreateSimpleRequest();
            var stopwatch = new Stopwatch();

            while (!stoppedCts.Token.IsCancellationRequested)
            {
                stopwatch.Restart();
                client.UnaryCall(request);
                stopwatch.Stop();

                // spec requires data point in nanoseconds.
                histogram.AddObservation(stopwatch.Elapsed.TotalSeconds * SecondsToNanos);

                timer.WaitForNext();
            }
        }

        private async Task RunUnaryAsync(Channel channel, IInterarrivalTimer timer)
        {
            var client = BenchmarkService.NewClient(channel);
            var request = CreateSimpleRequest();
            var stopwatch = new Stopwatch();

            while (!stoppedCts.Token.IsCancellationRequested)
            {
                stopwatch.Restart();
                await client.UnaryCallAsync(request);
                stopwatch.Stop();

                // spec requires data point in nanoseconds.
                histogram.AddObservation(stopwatch.Elapsed.TotalSeconds * SecondsToNanos);

                await timer.WaitForNextAsync();
            }
        }

        private async Task RunStreamingPingPongAsync(Channel channel, IInterarrivalTimer timer)
        {
            var client = BenchmarkService.NewClient(channel);
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
                    histogram.AddObservation(stopwatch.Elapsed.TotalSeconds * SecondsToNanos);

                    await timer.WaitForNextAsync();
                }

                // finish the streaming call
                await call.RequestStream.CompleteAsync();
                Assert.IsFalse(await call.ResponseStream.MoveNext());
            }
        }

        private async Task RunGenericStreamingAsync(Channel channel, IInterarrivalTimer timer)
        {
            var request = CreateByteBufferRequest();
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
                    histogram.AddObservation(stopwatch.Elapsed.TotalSeconds * SecondsToNanos);

                    await timer.WaitForNextAsync();
                }

                // finish the streaming call
                await call.RequestStream.CompleteAsync();
                Assert.IsFalse(await call.ResponseStream.MoveNext());
            }
        }

        private Action GetThreadBody(Channel channel, IInterarrivalTimer timer)
        {
            if (payloadConfig.PayloadCase == PayloadConfig.PayloadOneofCase.BytebufParams)
            {
                GrpcPreconditions.CheckArgument(clientType == ClientType.ASYNC_CLIENT, "Generic client only supports async API");
                GrpcPreconditions.CheckArgument(rpcType == RpcType.STREAMING, "Generic client only supports streaming calls");
                return () =>
                {
                    RunGenericStreamingAsync(channel, timer).Wait();
                };
            }

            GrpcPreconditions.CheckNotNull(payloadConfig.SimpleParams);
            if (clientType == ClientType.SYNC_CLIENT)
            {
                GrpcPreconditions.CheckArgument(rpcType == RpcType.UNARY, "Sync client can only be used for Unary calls in C#");
                return () => RunUnary(channel, timer);
            }
            else if (clientType == ClientType.ASYNC_CLIENT)
            {
                switch (rpcType)
                {
                    case RpcType.UNARY:
                        return () =>
                        {
                            RunUnaryAsync(channel, timer).Wait();
                        };
                    case RpcType.STREAMING:
                        return () =>
                        {
                            RunStreamingPingPongAsync(channel, timer).Wait();
                        };
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

        private byte[] CreateByteBufferRequest()
        {
            return new byte[payloadConfig.BytebufParams.ReqSize];
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
