#region Copyright notice and license

// Copyright 2019 The gRPC Authors.
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
using System.Net.Sockets;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Logging;
using Grpc.Core.Utils;
using Grpc.Core.Internal;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// See https://github.com/grpc/grpc/issues/18074, this test is meant to
    /// try to trigger the described bug.
    /// Runs interop tests in-process, with that client using a target
    /// name that using a target name that triggers interaction with
    /// external DNS servers (even though it resolves to the in-proc server).
    /// </summary>
    public class ExternalDnsWithTracingClientServerTest
    {
        Server server;
        Channel channel;
        TestService.TestServiceClient client;

        [OneTimeSetUp]
        public void Init()
        {
            // We only care about running this test on Windows (see #18074)
            // TODO(jtattermusch): We could run it on Linux and Mac as well,
            // but there are two issues.
            // 1. Due to https://github.com/grpc/grpc/issues/14963, setting the
            // enviroment variables actually has no effect on CoreCLR.
            // 2. On mono the test with enabled tracing sometimes times out
            // due to suspected mono-related issue on shutdown
            // See https://github.com/grpc/grpc/issues/18126
            if (PlatformApis.IsWindows)
            {
                Environment.SetEnvironmentVariable("GRPC_TRACE", "all");
                Environment.SetEnvironmentVariable("GRPC_VERBOSITY", "DEBUG");
                var newLogger = new SocketUsingLogger(GrpcEnvironment.Logger);
                GrpcEnvironment.SetLogger(newLogger);
            }
            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { TestService.BindService(new TestServiceImpl()) },
                Ports = { { "[::1]", ServerPort.PickUnused, ServerCredentials.Insecure } },
                // reduce the number of request call tokens to
                // avoid flooding the logs with token-related messages
                RequestCallTokensPerCompletionQueue = 3,
            };
            server.Start();
            int port = server.Ports.Single().BoundPort;
            channel = new Channel("loopback6.unittest.grpc.io", port, ChannelCredentials.Insecure);
            client = new TestService.TestServiceClient(channel);
        }

        [OneTimeTearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void EmptyUnary()
        {
            InteropClient.RunEmptyUnary(client);
        }
    }

    /// <summary>
    /// Logger which does some socket operation after delegating
    /// actual logging to its delegate logger. The main goal is to
    /// reset the current thread's WSA error status.
    /// The only reason for the delegateLogger is to continue
    /// to have this test display debug logs.
    /// </summary>
    internal sealed class SocketUsingLogger : ILogger
    {
        private ILogger delegateLogger;

        public SocketUsingLogger(ILogger delegateLogger) {
            this.delegateLogger = delegateLogger;
        }

        public void Debug(string message)
        {
            MyLog(() => delegateLogger.Debug(message));
        }

        public void Debug(string format, params object[] formatArgs)
        {
            MyLog(() => delegateLogger.Debug(format, formatArgs));
        }

        public void Error(string message)
        {
            MyLog(() => delegateLogger.Error(message));
        }

        public void Error(Exception exception, string message)
        {
            MyLog(() => delegateLogger.Error(exception, message));
        }

        public void Error(string format, params object[] formatArgs)
        {
            MyLog(() => delegateLogger.Error(format, formatArgs));
        }

        public ILogger ForType<T>()
        {
            return this;
        }

        public void Info(string message)
        {
            MyLog(() => delegateLogger.Info(message));
        }

        public void Info(string format, params object[] formatArgs)
        {
            MyLog(() => delegateLogger.Info(format, formatArgs));
        }

        public void Warning(string message)
        {
            MyLog(() => delegateLogger.Warning(message));
        }

        public void Warning(Exception exception, string message)
        {
            MyLog(() => delegateLogger.Warning(exception, message));
        }

        public void Warning(string format, params object[] formatArgs)
        {
            MyLog(() => delegateLogger.Warning(format, formatArgs));
        }

        private void MyLog(Action delegateLog)
        {
          delegateLog();
          // Create and close a socket, just in order to affect
          // the WSA (on Windows) error status of the current thread.
          Socket s = new Socket(AddressFamily.InterNetwork,
                                SocketType.Stream,
                                ProtocolType.Tcp);

          s.Dispose();
        }
    }
}
