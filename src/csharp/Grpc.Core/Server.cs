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
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Grpc.Core.Internal;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// gRPC server. A single server can server arbitrary number of services and can listen on more than one ports.
    /// </summary>
    public class Server
    {
        const int InitialAllowRpcTokenCountPerCq = 10;
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<Server>();

        readonly AtomicCounter activeCallCounter = new AtomicCounter();

        readonly ServiceDefinitionCollection serviceDefinitions;
        readonly ServerPortCollection ports;
        readonly GrpcEnvironment environment;
        readonly List<ChannelOption> options;
        readonly ServerSafeHandle handle;
        readonly object myLock = new object();

        readonly List<ServerServiceDefinition> serviceDefinitionsList = new List<ServerServiceDefinition>();
        readonly List<ServerPort> serverPortList = new List<ServerPort>();
        readonly Dictionary<string, IServerCallHandler> callHandlers = new Dictionary<string, IServerCallHandler>();
        readonly TaskCompletionSource<object> shutdownTcs = new TaskCompletionSource<object>();

        bool startRequested;
        volatile bool shutdownRequested;


        /// <summary>
        /// Creates a new server.
        /// </summary>
        public Server() : this(null)
        {
        }

        /// <summary>
        /// Creates a new server.
        /// </summary>
        /// <param name="options">Channel options.</param>
        public Server(IEnumerable<ChannelOption> options)
        {
            this.serviceDefinitions = new ServiceDefinitionCollection(this);
            this.ports = new ServerPortCollection(this);
            this.environment = GrpcEnvironment.AddRef();
            this.options = options != null ? new List<ChannelOption>(options) : new List<ChannelOption>();
            using (var channelArgs = ChannelOptions.CreateChannelArgs(this.options))
            {
                this.handle = ServerSafeHandle.NewServer(channelArgs);
            }

            foreach (var cq in environment.CompletionQueues)
            {
                this.handle.RegisterCompletionQueue(cq);
            }
            GrpcEnvironment.RegisterServer(this);
        }

        /// <summary>
        /// Services that will be exported by the server once started. Register a service with this
        /// server by adding its definition to this collection.
        /// </summary>
        public ServiceDefinitionCollection Services
        {
            get
            {
                return serviceDefinitions;
            }
        }

        /// <summary>
        /// Ports on which the server will listen once started. Register a port with this
        /// server by adding its definition to this collection.
        /// </summary>
        public ServerPortCollection Ports
        {
            get
            {
                return ports;
            }
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
        /// Starts the server.
        /// </summary>
        public void Start()
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(!startRequested);
                startRequested = true;
                
                handle.Start();

                // Starting with more than one AllowOneRpc tokens can significantly increase
                // unary RPC throughput.
                for (int i = 0; i < InitialAllowRpcTokenCountPerCq; i++)
                {
                    foreach (var cq in environment.CompletionQueues)
                    {
                        AllowOneRpc(cq);
                    }
                }
            }
        }

        /// <summary>
        /// Requests server shutdown and when there are no more calls being serviced,
        /// cleans up used resources. The returned task finishes when shutdown procedure
        /// is complete.
        /// </summary>
        /// <remarks>
        /// It is strongly recommended to shutdown all previously created servers before exiting from the process.
        /// </remarks>
        public Task ShutdownAsync()
        {
            return ShutdownInternalAsync(false);
        }

        /// <summary>
        /// Requests server shutdown while cancelling all the in-progress calls.
        /// The returned task finishes when shutdown procedure is complete.
        /// </summary>
        /// <remarks>
        /// It is strongly recommended to shutdown all previously created servers before exiting from the process.
        /// </remarks>
        public Task KillAsync()
        {
            return ShutdownInternalAsync(true);
        }

        internal void AddCallReference(object call)
        {
            activeCallCounter.Increment();

            bool success = false;
            handle.DangerousAddRef(ref success);
            GrpcPreconditions.CheckState(success);
        }

        internal void RemoveCallReference(object call)
        {
            handle.DangerousRelease();
            activeCallCounter.Decrement();
        }

        /// <summary>
        /// Shuts down the server.
        /// </summary>
        private async Task ShutdownInternalAsync(bool kill)
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(startRequested);
                GrpcPreconditions.CheckState(!shutdownRequested);
                shutdownRequested = true;
            }
            GrpcEnvironment.UnregisterServer(this);

            var cq = environment.CompletionQueues.First();  // any cq will do
            handle.ShutdownAndNotify(HandleServerShutdown, cq);
            if (kill)
            {
                handle.CancelAllCalls();
            }

            await ShutdownCompleteOrEnvironmentDeadAsync().ConfigureAwait(false);

            DisposeHandle();

            await GrpcEnvironment.ReleaseAsync().ConfigureAwait(false);
        }

        /// <summary>
        /// In case the environment's threadpool becomes dead, the shutdown completion will
        /// never be delivered, but we need to release the environment's handle anyway.
        /// </summary>
        private async Task ShutdownCompleteOrEnvironmentDeadAsync()
        {
            while (true)
            {
                var task = await Task.WhenAny(shutdownTcs.Task, Task.Delay(20)).ConfigureAwait(false);
                if (shutdownTcs.Task == task)
                {
                    return;
                }
                if (!environment.IsAlive)
                {
                    return;
                }
            }
        }

        /// <summary>
        /// Adds a service definition.
        /// </summary>
        private void AddServiceDefinitionInternal(ServerServiceDefinition serviceDefinition)
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckState(!startRequested);
                foreach (var entry in serviceDefinition.CallHandlers)
                {
                    callHandlers.Add(entry.Key, entry.Value);
                }
                serviceDefinitionsList.Add(serviceDefinition);
            }
        }

        /// <summary>
        /// Adds a listening port.
        /// </summary>
        private int AddPortInternal(ServerPort serverPort)
        {
            lock (myLock)
            {
                GrpcPreconditions.CheckNotNull(serverPort.Credentials, "serverPort");
                GrpcPreconditions.CheckState(!startRequested);
                var address = string.Format("{0}:{1}", serverPort.Host, serverPort.Port);
                int boundPort;
                using (var nativeCredentials = serverPort.Credentials.ToNativeCredentials())
                {
                    if (nativeCredentials != null)
                    {
                        boundPort = handle.AddSecurePort(address, nativeCredentials);
                    }
                    else
                    {
                        boundPort = handle.AddInsecurePort(address);
                    }
                }
                var newServerPort = new ServerPort(serverPort, boundPort);
                this.serverPortList.Add(newServerPort);
                return boundPort;
            }
        }

        /// <summary>
        /// Allows one new RPC call to be received by server.
        /// </summary>
        private void AllowOneRpc(CompletionQueueSafeHandle cq)
        {
            if (!shutdownRequested)
            {
                handle.RequestCall((success, ctx) => HandleNewServerRpc(success, ctx, cq), cq);
            }
        }

        private void DisposeHandle()
        {
            var activeCallCount = activeCallCounter.Count;
            if (activeCallCount > 0)
            {
                Logger.Warning("Server shutdown has finished but there are still {0} active calls for that server.", activeCallCount);
            }
            handle.Dispose();
        }

        /// <summary>
        /// Selects corresponding handler for given call and handles the call.
        /// </summary>
        private async Task HandleCallAsync(ServerRpcNew newRpc, CompletionQueueSafeHandle cq)
        {
            try
            {
                IServerCallHandler callHandler;
                if (!callHandlers.TryGetValue(newRpc.Method, out callHandler))
                {
                    callHandler = NoSuchMethodCallHandler.Instance;
                }
                await callHandler.HandleCall(newRpc, cq).ConfigureAwait(false);
            }
            catch (Exception e)
            {
                Logger.Warning(e, "Exception while handling RPC.");
            }
        }

        /// <summary>
        /// Handles the native callback.
        /// </summary>
        private void HandleNewServerRpc(bool success, BatchContextSafeHandle ctx, CompletionQueueSafeHandle cq)
        {
			Task.Run(() => AllowOneRpc(cq));

            if (success)
            {
                ServerRpcNew newRpc = ctx.GetServerRpcNew(this);

                // after server shutdown, the callback returns with null call
                if (!newRpc.Call.IsInvalid)
                {
                    HandleCallAsync(newRpc, cq);  // we don't need to await.
                }
            }
        }

        /// <summary>
        /// Handles native callback.
        /// </summary>
        private void HandleServerShutdown(bool success, BatchContextSafeHandle ctx)
        {
            shutdownTcs.SetResult(null);
        }

        /// <summary>
        /// Collection of service definitions.
        /// </summary>
        public class ServiceDefinitionCollection : IEnumerable<ServerServiceDefinition>
        {
            readonly Server server;

            internal ServiceDefinitionCollection(Server server)
            {
                this.server = server;
            }

            /// <summary>
            /// Adds a service definition to the server. This is how you register
            /// handlers for a service with the server. Only call this before Start().
            /// </summary>
            public void Add(ServerServiceDefinition serviceDefinition)
            {
                server.AddServiceDefinitionInternal(serviceDefinition);
            }

            /// <summary>
            /// Gets enumerator for this collection.
            /// </summary>
            public IEnumerator<ServerServiceDefinition> GetEnumerator()
            {
                return server.serviceDefinitionsList.GetEnumerator();
            }

            IEnumerator IEnumerable.GetEnumerator()
            {
                return server.serviceDefinitionsList.GetEnumerator();
            }
        }

        /// <summary>
        /// Collection of server ports.
        /// </summary>
        public class ServerPortCollection : IEnumerable<ServerPort>
        {
            readonly Server server;

            internal ServerPortCollection(Server server)
            {
                this.server = server;
            }

            /// <summary>
            /// Adds a new port on which server should listen.
            /// Only call this before Start().
            /// <returns>The port on which server will be listening.</returns>
            /// </summary>
            public int Add(ServerPort serverPort)
            {
                return server.AddPortInternal(serverPort);
            }

            /// <summary>
            /// Adds a new port on which server should listen.
            /// <returns>The port on which server will be listening.</returns>
            /// </summary>
            /// <param name="host">the host</param>
            /// <param name="port">the port. If zero, an unused port is chosen automatically.</param>
            /// <param name="credentials">credentials to use to secure this port.</param>
            public int Add(string host, int port, ServerCredentials credentials)
            {
                return Add(new ServerPort(host, port, credentials));
            }

            /// <summary>
            /// Gets enumerator for this collection.
            /// </summary>
            public IEnumerator<ServerPort> GetEnumerator()
            {
                return server.serverPortList.GetEnumerator();
            }

            IEnumerator IEnumerable.GetEnumerator()
            {
                return server.serverPortList.GetEnumerator();
            }
        }
    }
}
