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
    /// Helper methods to start server runners for performance testing.
    /// </summary>
    public class ServerRunners
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<ServerRunners>();

        /// <summary>
        /// Creates a started server runner.
        /// </summary>
        public static IServerRunner CreateStarted(ServerConfig config)
        {
            Logger.Debug("ServerConfig: {0}", config);
            var credentials = config.SecurityParams != null ? TestCredentials.CreateSslServerCredentials() : ServerCredentials.Insecure;

            if (config.AsyncServerThreads != 0)
            {
                Logger.Warning("ServerConfig.AsyncServerThreads is not supported for C#. Ignoring the value");
            }
            if (config.CoreLimit != 0)
            {
                Logger.Warning("ServerConfig.CoreLimit is not supported for C#. Ignoring the value");
            }
            if (config.CoreList.Count > 0)
            {
                Logger.Warning("ServerConfig.CoreList is not supported for C#. Ignoring the value");
            }

            ServerServiceDefinition service = null;
            if (config.ServerType == ServerType.AsyncServer)
            {
                GrpcPreconditions.CheckArgument(config.PayloadConfig == null,
                    "ServerConfig.PayloadConfig shouldn't be set for BenchmarkService based server.");    
                service = BenchmarkService.BindService(new BenchmarkServiceImpl());
            }
            else if (config.ServerType == ServerType.AsyncGenericServer)
            {
                var genericService = new GenericServiceImpl(config.PayloadConfig.BytebufParams.RespSize);
                service = GenericService.BindHandler(genericService.StreamingCall);
            }
            else
            {
                throw new ArgumentException("Unsupported ServerType");
            }

            var channelOptions = new List<ChannelOption>(config.ChannelArgs.Select((arg) => arg.ToChannelOption()));
            var server = new Server(channelOptions)
            {
                Services = { service },
                Ports = { new ServerPort("[::]", config.Port, credentials) }
            };

            server.Start();
            return new ServerRunnerImpl(server);
        }

        private class GenericServiceImpl
        {
            readonly byte[] response;

            public GenericServiceImpl(int responseSize)
            {
                this.response = new byte[responseSize];
            }

            /// <summary>
            /// Generic streaming call handler.
            /// </summary>
            public async Task StreamingCall(IAsyncStreamReader<byte[]> requestStream, IServerStreamWriter<byte[]> responseStream, ServerCallContext context)
            {
                await requestStream.ForEachAsync(async request =>
                {
                    await responseStream.WriteAsync(response);
                });
            }
        }
    }

    /// <summary>
    /// Server runner.
    /// </summary>
    public class ServerRunnerImpl : IServerRunner
    {
        readonly Server server;
        readonly WallClockStopwatch wallClockStopwatch = new WallClockStopwatch();

        public ServerRunnerImpl(Server server)
        {
            this.server = GrpcPreconditions.CheckNotNull(server);
        }

        public int BoundPort
        {
            get
            {
                return server.Ports.Single().BoundPort;
            }
        }

        /// <summary>
        /// Gets server stats.
        /// </summary>
        /// <returns>The stats.</returns>
        public ServerStats GetStats(bool reset)
        {
            var secondsElapsed = wallClockStopwatch.GetElapsedSnapshot(reset).TotalSeconds;

            GrpcEnvironment.Logger.Info("[ServerRunner.GetStats] GC collection counts: gen0 {0}, gen1 {1}, gen2 {2}, (seconds since last reset {3})",
                GC.CollectionCount(0), GC.CollectionCount(1), GC.CollectionCount(2), secondsElapsed);

            // TODO: populate user time and system time
            return new ServerStats
            {
                TimeElapsed = secondsElapsed,
                TimeUser = 0,
                TimeSystem = 0
            };
        }

        /// <summary>
        /// Asynchronously stops the server.
        /// </summary>
        /// <returns>Task that finishes when server has shutdown.</returns>
        public Task StopAsync()
        {
            return server.ShutdownAsync();
        }
    }        
}
