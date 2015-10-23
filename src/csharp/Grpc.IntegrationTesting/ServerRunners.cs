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
using Grpc.Core.Utils;
using NUnit.Framework;
using Grpc.Testing;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Helper methods to start server runners for performance testing.
    /// </summary>
    public static class ServerRunners
    {
        /// <summary>
        /// Creates a started server runner.
        /// </summary>
        public static IServerRunner CreateStarted(ServerConfig config)
        {
            Grpc.Core.Utils.Preconditions.CheckArgument(config.ServerType == ServerType.ASYNC_SERVER);
            var credentials = config.SecurityParams != null ? TestCredentials.CreateSslServerCredentials() : ServerCredentials.Insecure;

            // TODO: qps_driver needs to setup payload properly...
            int responseSize = config.PayloadConfig != null ? config.PayloadConfig.SimpleParams.RespSize : 0;
            var server = new Server
            {
                Services = { BenchmarkService.BindService(new BenchmarkServiceImpl(responseSize)) },
                Ports = { new ServerPort("0.0.0.0", config.Port, credentials) }
            };

            server.Start();
            return new ServerRunnerImpl(server);
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
            this.server = Grpc.Core.Utils.Preconditions.CheckNotNull(server);
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
