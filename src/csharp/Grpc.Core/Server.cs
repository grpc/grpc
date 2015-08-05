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
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Grpc.Core.Internal;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// A gRPC server.
    /// </summary>
    public class Server
    {
        /// <summary>
        /// Pass this value as port to have the server choose an unused listening port for you.
        /// </summary>
        public const int PickUnusedPort = 0;

        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<Server>();

        readonly GrpcEnvironment environment;
        readonly List<ChannelOption> options;
        readonly ServerSafeHandle handle;
        readonly object myLock = new object();

        readonly Dictionary<string, IServerCallHandler> callHandlers = new Dictionary<string, IServerCallHandler>();
        readonly TaskCompletionSource<object> shutdownTcs = new TaskCompletionSource<object>();

        bool startRequested;
        bool shutdownRequested;

        /// <summary>
        /// Create a new server.
        /// </summary>
        /// <param name="options">Channel options.</param>
        public Server(IEnumerable<ChannelOption> options = null)
        {
            this.environment = GrpcEnvironment.GetInstance();
            this.options = options != null ? new List<ChannelOption>(options) : new List<ChannelOption>();
            using (var channelArgs = ChannelOptions.CreateChannelArgs(this.options))
            {
                this.handle = ServerSafeHandle.NewServer(environment.CompletionQueue, channelArgs);
            }
        }

        /// <summary>
        /// Adds a service definition to the server. This is how you register
        /// handlers for a service with the server.
        /// Only call this before Start().
        /// </summary>
        public void AddServiceDefinition(ServerServiceDefinition serviceDefinition)
        {
            lock (myLock)
            {
                Preconditions.CheckState(!startRequested);
                foreach (var entry in serviceDefinition.CallHandlers)
                {
                    callHandlers.Add(entry.Key, entry.Value);
                }
            }
        }

        /// <summary>
        /// Add a port on which server should listen.
        /// Only call this before Start().
        /// </summary>
        /// <returns>The port on which server will be listening.</returns>
        /// <param name="host">the host</param>
        /// <param name="port">the port. If zero, an unused port is chosen automatically.</param>
        public int AddPort(string host, int port, ServerCredentials credentials)
        {
            lock (myLock)
            {
                Preconditions.CheckNotNull(credentials);
                Preconditions.CheckState(!startRequested);
                var address = string.Format("{0}:{1}", host, port);
                using (var nativeCredentials = credentials.ToNativeCredentials())
                {
                    if (nativeCredentials != null)
                    {
                        return handle.AddSecurePort(address, nativeCredentials);
                    }
                    else
                    {
                        return handle.AddInsecurePort(address);
                    }
                }
            }
        }

        /// <summary>
        /// Starts the server.
        /// </summary>
        public void Start()
        {
            lock (myLock)
            {
                Preconditions.CheckState(!startRequested);
                startRequested = true;
                
                handle.Start();
                AllowOneRpc();
            }
        }

        /// <summary>
        /// Requests server shutdown and when there are no more calls being serviced,
        /// cleans up used resources. The returned task finishes when shutdown procedure
        /// is complete.
        /// </summary>
        public async Task ShutdownAsync()
        {
            lock (myLock)
            {
                Preconditions.CheckState(startRequested);
                Preconditions.CheckState(!shutdownRequested);
                shutdownRequested = true;
            }

            handle.ShutdownAndNotify(HandleServerShutdown, environment);
            await shutdownTcs.Task;
            handle.Dispose();
        }

        /// <summary>
        /// To allow awaiting termination of the server.
        /// </summary>
        public Task ShutdownTask
        {
            get
            {
                return shutdownTcs.Task;
            }
        }

        /// <summary>
        /// Requests server shutdown while cancelling all the in-progress calls.
        /// The returned task finishes when shutdown procedure is complete.
        /// </summary>
        public async Task KillAsync()
        {
            lock (myLock)
            {
                Preconditions.CheckState(startRequested);
                Preconditions.CheckState(!shutdownRequested);
                shutdownRequested = true;
            }

            handle.ShutdownAndNotify(HandleServerShutdown, environment);
            handle.CancelAllCalls();
            await shutdownTcs.Task;
            handle.Dispose();
        }

        /// <summary>
        /// Allows one new RPC call to be received by server.
        /// </summary>
        private void AllowOneRpc()
        {
            lock (myLock)
            {
                if (!shutdownRequested)
                {
                    handle.RequestCall(HandleNewServerRpc, environment);
                }
            }
        }

        /// <summary>
        /// Selects corresponding handler for given call and handles the call.
        /// </summary>
        private async Task HandleCallAsync(ServerRpcNew newRpc)
        {
            try
            {
                IServerCallHandler callHandler;
                if (!callHandlers.TryGetValue(newRpc.Method, out callHandler))
                {
                    callHandler = NoSuchMethodCallHandler.Instance;
                }
                await callHandler.HandleCall(newRpc, environment);
            }
            catch (Exception e)
            {
                Logger.Warning(e, "Exception while handling RPC.");
            }
        }

        /// <summary>
        /// Handles the native callback.
        /// </summary>
        private void HandleNewServerRpc(bool success, BatchContextSafeHandle ctx)
        {
            if (success)
            {
                ServerRpcNew newRpc = ctx.GetServerRpcNew();

                // after server shutdown, the callback returns with null call
                if (!newRpc.Call.IsInvalid)
                {
                    Task.Run(async () => await HandleCallAsync(newRpc));
                }
            }

            AllowOneRpc();
        }

        /// <summary>
        /// Handles native callback.
        /// </summary>
        private void HandleServerShutdown(bool success, BatchContextSafeHandle ctx)
        {
            shutdownTcs.SetResult(null);
        }
    }
}
