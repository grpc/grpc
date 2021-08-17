#region Copyright notice and license

// Copyright 2020 The gRPC Authors
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
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

using CommandLine;
using Grpc.Core;
using Grpc.Core.Logging;
using Grpc.Core.Internal;
using Grpc.Testing;

namespace Grpc.IntegrationTesting
{
    public class XdsInteropClient
    {
        internal class ClientOptions
        {
            [Option("num_channels", Default = 1)]
            public int NumChannels { get; set; }

            [Option("qps", Default = 1)]

            // The desired QPS per channel, for each type of RPC.
            public int Qps { get; set; }

            [Option("server", Default = "localhost:8080")]
            public string Server { get; set; }

            [Option("stats_port", Default = 8081)]
            public int StatsPort { get; set; }

            [Option("rpc_timeout_sec", Default = 30)]
            public int RpcTimeoutSec { get; set; }

            [Option("print_response", Default = false)]
            public bool PrintResponse { get; set; }

            // Types of RPCs to make, ',' separated string. RPCs can be EmptyCall or UnaryCall
            [Option("rpc", Default = "UnaryCall")]
            public string Rpc { get; set; }

            // The metadata to send with each RPC, in the format EmptyCall:key1:value1,UnaryCall:key2:value2
            [Option("metadata", Default = null)]
            public string Metadata { get; set; }
        }

        internal enum RpcType
        {
            UnaryCall,
            EmptyCall
        }

        ClientOptions options;

        StatsWatcher statsWatcher = new StatsWatcher();

        List<RpcType> rpcs;
        Dictionary<RpcType, Metadata> metadata;

        // make watcher accessible by tests
        internal StatsWatcher StatsWatcher => statsWatcher;

        internal XdsInteropClient(ClientOptions options)
        {
            this.options = options;
            this.rpcs = ParseRpcArgument(this.options.Rpc);
            this.metadata = ParseMetadataArgument(this.options.Metadata);
        }

        public static void Run(string[] args)
        {
            GrpcEnvironment.SetLogger(new ConsoleLogger());
            var parserResult = Parser.Default.ParseArguments<ClientOptions>(args)
                .WithNotParsed(errors => Environment.Exit(1))
                .WithParsed(options =>
                {
                    var xdsInteropClient = new XdsInteropClient(options);
                    xdsInteropClient.RunAsync().Wait();
                });
        }

        private async Task RunAsync()
        {
            var server = new Server
            {
                Services = { LoadBalancerStatsService.BindService(new LoadBalancerStatsServiceImpl(statsWatcher)) }
            };

            string host = "0.0.0.0";
            server.Ports.Add(host, options.StatsPort, ServerCredentials.Insecure);
            Console.WriteLine($"Running server on {host}:{options.StatsPort}");
            server.Start();

            var cancellationTokenSource = new CancellationTokenSource();
            await RunChannelsAsync(cancellationTokenSource.Token);

            await server.ShutdownAsync();
        }

        // method made internal to make it runnable by tests
        internal async Task RunChannelsAsync(CancellationToken cancellationToken)
        {
            var channelTasks = new List<Task>();
            for (int channelId = 0; channelId < options.NumChannels; channelId++)
            {
                var channelTask = RunSingleChannelAsync(channelId, cancellationToken);
                channelTasks.Add(channelTask);
            }

            for (int channelId = 0; channelId < options.NumChannels; channelId++)
            {
                await channelTasks[channelId];
            }
        }

        private async Task RunSingleChannelAsync(int channelId, CancellationToken cancellationToken)
        {
            Console.WriteLine($"Starting channel {channelId}");
            var channel = new Channel(options.Server, ChannelCredentials.Insecure);
            var client = new TestService.TestServiceClient(channel);

            var inflightTasks = new List<Task>();
            long rpcsStarted = 0;
            var stopwatch = Stopwatch.StartNew();
            while (!cancellationToken.IsCancellationRequested)
            {
                foreach (var rpcType in rpcs)
                {
                    inflightTasks.Add(RunSingleRpcAsync(client, cancellationToken, rpcType));
                    rpcsStarted++;
                }

                // only cleanup calls that have already completed, calls that are still inflight will be cleaned up later.
                await CleanupCompletedTasksAsync(inflightTasks);

                // if needed, wait a bit before we start the next RPC.
                int nextDueInMillis = (int) Math.Max(0, (1000 * rpcsStarted / options.Qps / rpcs.Count) - stopwatch.ElapsedMilliseconds);
                if (nextDueInMillis > 0)
                {
                    await Task.Delay(nextDueInMillis);
                }
            }
            stopwatch.Stop();

            Console.WriteLine($"Shutting down channel {channelId}");
            await channel.ShutdownAsync();
            Console.WriteLine($"Channel shutdown {channelId}");
        }

        private async Task RunSingleRpcAsync(TestService.TestServiceClient client, CancellationToken cancellationToken, RpcType rpcType)
        {
            long rpcId = statsWatcher.RpcIdGenerator.Increment();
            try
            {
                // metadata to send with the RPC
                var headers = new Metadata();
                if (metadata.ContainsKey(rpcType))
                {
                    headers = metadata[rpcType];
                    if (headers.Count > 0)
                    {
                        var printableHeaders = "[" + string.Join(", ", headers) + "]";
                    }
                }

                if (rpcType == RpcType.UnaryCall)
                {

                    var call = client.UnaryCallAsync(new SimpleRequest(),
                        new CallOptions(headers: headers, cancellationToken: cancellationToken, deadline: DateTime.UtcNow.AddSeconds(options.RpcTimeoutSec)));

                    var response = await call;
                    var hostname = (await call.ResponseHeadersAsync).GetValue("hostname") ?? response.Hostname;
                    statsWatcher.OnRpcComplete(rpcId, rpcType, hostname);
                    if (options.PrintResponse)
                    {
                        Console.WriteLine($"Got response {response}");
                    }
                }
                else if (rpcType == RpcType.EmptyCall)
                {
                    var call = client.EmptyCallAsync(new Empty(),
                        new CallOptions(headers: headers, cancellationToken: cancellationToken, deadline: DateTime.UtcNow.AddSeconds(options.RpcTimeoutSec)));

                    var response = await call;
                    var hostname = (await call.ResponseHeadersAsync).GetValue("hostname");
                    statsWatcher.OnRpcComplete(rpcId, rpcType, hostname);
                    if (options.PrintResponse)
                    {
                        Console.WriteLine($"Got response {response}");
                    }
                }
                else
                {
                    throw new InvalidOperationException($"Unsupported RPC type ${rpcType}");
                }
            }
            catch (RpcException ex)
            {
                statsWatcher.OnRpcComplete(rpcId, rpcType, null);
                if (options.PrintResponse)
                {
                    Console.WriteLine($"RPC {rpcId} failed: {ex}");
                }
            }
        }

        private async Task CleanupCompletedTasksAsync(List<Task> tasks)
        {
            var toRemove = new List<Task>();
            foreach (var task in tasks)
            {
                if (task.IsCompleted)
                {
                    // awaiting tasks that have already completed should be instantaneous
                    await task;
                }
                toRemove.Add(task);
            }
            foreach (var task in toRemove)
            {
                tasks.Remove(task);
            }
        }

        private static List<RpcType> ParseRpcArgument(string rpcArg)
        {
            var result = new List<RpcType>();
            foreach (var part in rpcArg.Split(','))
            {
                result.Add(ParseRpc(part));
            }
            return result;
        }

        private static RpcType ParseRpc(string rpc)
        {
            switch (rpc)
            {
                case "UnaryCall":
                    return RpcType.UnaryCall;
                case "EmptyCall":
                    return RpcType.EmptyCall;
                default:
                    throw new ArgumentException($"Unknown RPC: \"{rpc}\"");
            }
        }

        private static Dictionary<RpcType, Metadata> ParseMetadataArgument(string metadataArg)
        {
            var rpcMetadata = new Dictionary<RpcType, Metadata>();
            if (string.IsNullOrEmpty(metadataArg))
            {
                return rpcMetadata;
            }

            foreach (var metadata in metadataArg.Split(','))
            {
                var parts = metadata.Split(':');
                if (parts.Length != 3)
                {
                    throw new ArgumentException($"Invalid metadata: \"{metadata}\"");
                }
                var rpc = ParseRpc(parts[0]);
                var key = parts[1];
                var value = parts[2];

                var md = new Metadata { {key, value} };

                if (rpcMetadata.ContainsKey(rpc))
                {
                    var existingMetadata = rpcMetadata[rpc];
                    foreach (var entry in md)
                    {
                        existingMetadata.Add(entry);
                    }
                }
                else
                {
                    rpcMetadata.Add(rpc, md);
                }
            }
            return rpcMetadata;
        }
    }

    internal class StatsWatcher
    {
        private readonly object myLock = new object();
        private readonly AtomicCounter rpcIdGenerator = new AtomicCounter(0);

        private long? firstAcceptedRpcId;
        private int numRpcsWanted;
        private int rpcsCompleted;
        private int rpcsNoHostname;
        private Dictionary<string, int> rpcsByHostname;
        private Dictionary<string, Dictionary<string, int>> rpcsByMethod;

        public AtomicCounter RpcIdGenerator => rpcIdGenerator;

        public StatsWatcher()
        {
            Reset();
        }

        public void OnRpcComplete(long rpcId, XdsInteropClient.RpcType rpcType, string responseHostname)
        {
            lock (myLock)
            {
                if (!firstAcceptedRpcId.HasValue || rpcId < firstAcceptedRpcId || rpcId >= firstAcceptedRpcId + numRpcsWanted)
                {
                    return;
                }

                if (string.IsNullOrEmpty(responseHostname))
                {
                    rpcsNoHostname ++;
                }
                else 
                {
                    // update rpcsByHostname
                    if (!rpcsByHostname.ContainsKey(responseHostname))
                    {
                        rpcsByHostname[responseHostname] = 0;
                    }
                    rpcsByHostname[responseHostname] += 1;

                    // update rpcsByMethod
                    var method = rpcType.ToString();
                    if (!rpcsByMethod.ContainsKey(method))
                    {
                        rpcsByMethod[method] = new Dictionary<string, int>();
                    }
                    if (!rpcsByMethod[method].ContainsKey(responseHostname))
                    {
                        rpcsByMethod[method][responseHostname] = 0;
                    }
                    rpcsByMethod[method][responseHostname] += 1;
                }
                rpcsCompleted += 1;

                if (rpcsCompleted >= numRpcsWanted)
                {
                    Monitor.Pulse(myLock);
                }
            }
        }

        public void Reset()
        {
            lock (myLock)
            {
                firstAcceptedRpcId = null;
                numRpcsWanted = 0;
                rpcsCompleted = 0;
                rpcsNoHostname = 0;
                rpcsByHostname = new Dictionary<string, int>();
                rpcsByMethod = new Dictionary<string, Dictionary<string, int>>();
            }
        }

        public LoadBalancerStatsResponse WaitForRpcStatsResponse(int rpcsWanted, int timeoutSec)
        {
            lock (myLock)
            {
                if (firstAcceptedRpcId.HasValue)
                {
                    throw new InvalidOperationException("StateWatcher is already collecting stats.");
                }
                // we are only interested in the next numRpcsWanted RPCs
                firstAcceptedRpcId = rpcIdGenerator.Count + 1;
                numRpcsWanted = rpcsWanted;

                var deadline = DateTime.UtcNow.AddSeconds(timeoutSec);
                while (true)
                {
                    var timeoutMillis = Math.Max((int)(deadline - DateTime.UtcNow).TotalMilliseconds, 0);
                    if (!Monitor.Wait(myLock, timeoutMillis) || rpcsCompleted >= rpcsWanted)
                    {
                        // we collected enough RPCs, or timed out waiting
                        var response = new LoadBalancerStatsResponse { NumFailures = rpcsNoHostname };
                        response.RpcsByPeer.Add(rpcsByHostname);
                        
                        response.RpcsByMethod.Clear();
                        foreach (var methodEntry in rpcsByMethod)
                        {
                            var rpcsByPeer = new LoadBalancerStatsResponse.Types.RpcsByPeer();
                            rpcsByPeer.RpcsByPeer_.Add(methodEntry.Value);
                            response.RpcsByMethod[methodEntry.Key] = rpcsByPeer;
                        }
                        Reset();
                        return response;
                    }
                }
            }
        }
    }

    /// <summary>
    /// Implementation of LoadBalancerStatsService server
    /// </summary>
    internal class LoadBalancerStatsServiceImpl : LoadBalancerStatsService.LoadBalancerStatsServiceBase
    {
        StatsWatcher statsWatcher;

        public LoadBalancerStatsServiceImpl(StatsWatcher statsWatcher)
        {
            this.statsWatcher = statsWatcher;
        }

        public override async Task<LoadBalancerStatsResponse> GetClientStats(LoadBalancerStatsRequest request, ServerCallContext context)
        {
            // run as a task to avoid blocking
            var response = await Task.Run(() => statsWatcher.WaitForRpcStatsResponse(request.NumRpcs, request.TimeoutSec));
            Console.WriteLine($"Returning stats {response} (num of requested RPCs: {request.NumRpcs})");
            return response;
        }
    }
}
