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

        // TODO(jtattermusch) : make sure the delegate doesn't get garbage collected while
        // native callbacks are in the completion queue.
        readonly CompletionCallbackDelegate serverShutdownHandler;
        readonly CompletionCallbackDelegate newServerRpcHandler;

        readonly ServerSafeHandle handle;
        readonly object myLock = new object();

        readonly Dictionary<string, IServerCallHandler> callHandlers = new Dictionary<string, IServerCallHandler>();
        readonly TaskCompletionSource<object> shutdownTcs = new TaskCompletionSource<object>();

        bool startRequested;
        bool shutdownRequested;

        public Server()
        {
            this.handle = ServerSafeHandle.NewServer(GetCompletionQueue(), IntPtr.Zero);
            this.newServerRpcHandler = HandleNewServerRpc;
            this.serverShutdownHandler = HandleServerShutdown;
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
        /// Add a non-secure port on which server should listen.
        /// Only call this before Start().
        /// </summary>
        /// <returns>The port on which server will be listening.</returns>
        /// <param name="host">the host</param>
        /// <param name="port">the port. If zero, an unused port is chosen automatically.</param>
        public int AddListeningPort(string host, int port)
        {
            return AddListeningPortInternal(host, port, null);
        }

        /// <summary>
        /// Add a non-secure port on which server should listen.
        /// Only call this before Start().
        /// </summary>
        /// <returns>The port on which server will be listening.</returns>
        /// <param name="host">the host</param>
        /// <param name="port">the port. If zero, , an unused port is chosen automatically.</param>
        public int AddListeningPort(string host, int port, ServerCredentials credentials)
        {
            Preconditions.CheckNotNull(credentials);
            return AddListeningPortInternal(host, port, credentials);
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

            handle.ShutdownAndNotify(serverShutdownHandler);
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

        public void Kill()
        {
            handle.Dispose();
        }

        private int AddListeningPortInternal(string host, int port, ServerCredentials credentials)
        {
            lock (myLock)
            {
                Preconditions.CheckState(!startRequested);    
                var address = string.Format("{0}:{1}", host, port);
                if (credentials != null)
                {
                    using (var nativeCredentials = credentials.ToNativeCredentials())
                    {
                        return handle.AddListeningPort(address, nativeCredentials);
                    }
                }
                else
                {
                    return handle.AddListeningPort(address);    
                }
            }
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
                    handle.RequestCall(GetCompletionQueue(), newServerRpcHandler);
                }
            }
        }

        /// <summary>
        /// Selects corresponding handler for given call and handles the call.
        /// </summary>
        private async Task InvokeCallHandler(CallSafeHandle call, string method)
        {
            try
            {
                IServerCallHandler callHandler;
                if (!callHandlers.TryGetValue(method, out callHandler))
                {
                    callHandler = new NoSuchMethodCallHandler();
                }
                await callHandler.HandleCall(method, call, GetCompletionQueue());
            }
            catch (Exception e)
            {
                Console.WriteLine("Exception while handling RPC: " + e);
            }
        }

        /// <summary>
        /// Handles the native callback.
        /// </summary>
        private void HandleNewServerRpc(bool success, IntPtr batchContextPtr)
        {
            try
            {
                var ctx = new BatchContextSafeHandleNotOwned(batchContextPtr);

                // TODO: handle error

                CallSafeHandle call = ctx.GetServerRpcNewCall();
                string method = ctx.GetServerRpcNewMethod();

                // after server shutdown, the callback returns with null call
                if (!call.IsInvalid)
                {
                    Task.Run(async () => await InvokeCallHandler(call, method));
                }

                AllowOneRpc();
            }
            catch (Exception e)
            {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        /// <summary>
        /// Handles native callback.
        /// </summary>
        private void HandleServerShutdown(bool success, IntPtr batchContextPtr)
        {
            try
            {
                shutdownTcs.SetResult(null);
            }
            catch (Exception e)
            {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private static CompletionQueueSafeHandle GetCompletionQueue()
        {
            return GrpcEnvironment.ThreadPool.CompletionQueue;
        }
    }
}
