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

namespace Grpc.Core
{
    /// <summary>
    /// Server is implemented only to be able to do
    /// in-process testing.
    /// </summary>
    public class Server
    {
        // TODO: make sure the delegate doesn't get garbage collected while
        // native callbacks are in the completion queue.
        readonly ServerShutdownCallbackDelegate serverShutdownHandler;
        readonly CompletionCallbackDelegate newServerRpcHandler;

        readonly BlockingCollection<NewRpcInfo> newRpcQueue = new BlockingCollection<NewRpcInfo>();
        readonly ServerSafeHandle handle;

        readonly Dictionary<string, IServerCallHandler> callHandlers = new Dictionary<string, IServerCallHandler>();

        readonly TaskCompletionSource<object> shutdownTcs = new TaskCompletionSource<object>();

        public Server()
        {
            this.handle = ServerSafeHandle.NewServer(GetCompletionQueue(), IntPtr.Zero);
            this.newServerRpcHandler = HandleNewServerRpc;
            this.serverShutdownHandler = HandleServerShutdown;
        }

        // only call this before Start()
        public void AddServiceDefinition(ServerServiceDefinition serviceDefinition)
        {
            foreach (var entry in serviceDefinition.CallHandlers)
            {
                callHandlers.Add(entry.Key, entry.Value);
            }
        }

        // only call before Start()
        public int AddListeningPort(string addr)
        {
            return handle.AddListeningPort(addr);
        }

        // only call before Start()
        public int AddListeningPort(string addr, ServerCredentials credentials)
        {
            using (var nativeCredentials = credentials.ToNativeCredentials())
            {
                return handle.AddListeningPort(addr, nativeCredentials);
            }
        }

        public void Start()
        {
            handle.Start();

            // TODO: this basically means the server is single threaded....
            StartHandlingRpcs();
        }

        /// <summary>
        /// Requests and handles single RPC call.
        /// </summary>
        internal void RunRpc()
        {
            AllowOneRpc();

            try
            {
                var rpcInfo = newRpcQueue.Take();

                // Console.WriteLine("Server received RPC " + rpcInfo.Method);

                IServerCallHandler callHandler;
                if (!callHandlers.TryGetValue(rpcInfo.Method, out callHandler))
                {
                    callHandler = new NoSuchMethodCallHandler();
                }
                callHandler.StartCall(rpcInfo.Method, rpcInfo.Call, GetCompletionQueue());
            }
            catch (Exception e)
            {
                Console.WriteLine("Exception while handling RPC: " + e);
            }
        }

        /// <summary>
        /// Requests server shutdown and when there are no more calls being serviced,
        /// cleans up used resources.
        /// </summary>
        /// <returns>The async.</returns>
        public async Task ShutdownAsync()
        {
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

        private async Task StartHandlingRpcs()
        {
            while (true)
            {
                await Task.Factory.StartNew(RunRpc);
            }
        }

        private void AllowOneRpc()
        {
            AssertCallOk(handle.RequestCall(GetCompletionQueue(), newServerRpcHandler));
        }

        private void HandleNewServerRpc(GRPCOpError error, IntPtr batchContextPtr)
        {
            try
            {
                var ctx = new BatchContextSafeHandleNotOwned(batchContextPtr);

                if (error != GRPCOpError.GRPC_OP_OK)
                {
                    // TODO: handle error
                }

                var rpcInfo = new NewRpcInfo(ctx.GetServerRpcNewCall(), ctx.GetServerRpcNewMethod());

                // after server shutdown, the callback returns with null call
                if (!rpcInfo.Call.IsInvalid)
                {
                    newRpcQueue.Add(rpcInfo);
                }
            }
            catch (Exception e)
            {
                Console.WriteLine("Caught exception in a native handler: " + e);
            }
        }

        private void HandleServerShutdown(IntPtr eventPtr)
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

        private static void AssertCallOk(GRPCCallError callError)
        {
            Trace.Assert(callError == GRPCCallError.GRPC_CALL_OK, "Status not GRPC_CALL_OK");
        }

        private static CompletionQueueSafeHandle GetCompletionQueue()
        {
            return GrpcEnvironment.ThreadPool.CompletionQueue;
        }

        private struct NewRpcInfo
        {
            private CallSafeHandle call;
            private string method;

            public NewRpcInfo(CallSafeHandle call, string method)
            {
                this.call = call;
                this.method = method;
            }

            public CallSafeHandle Call
            {
                get
                {
                    return this.call;
                }
            }

            public string Method
            {
                get
                {
                    return this.method;
                }
            }
        }
    }
}
