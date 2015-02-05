using System;
using System.Runtime.InteropServices;
using System.Diagnostics;
using System.Threading.Tasks;
using System.Collections.Concurrent;
using System.Collections.Generic;
using Google.GRPC.Core.Internal;

namespace Google.GRPC.Core
{
    /// <summary>
    /// Server is implemented only to be able to do
    /// in-process testing.
    /// </summary>
    public class Server
    {
        // TODO: make sure the delegate doesn't get garbage collected while 
        // native callbacks are in the completion queue.
        readonly EventCallbackDelegate newRpcHandler;
        readonly EventCallbackDelegate serverShutdownHandler;

        readonly BlockingCollection<NewRpcInfo> newRpcQueue = new BlockingCollection<NewRpcInfo>();
        readonly ServerSafeHandle handle;

        readonly Dictionary<string, IServerCallHandler> callHandlers = new Dictionary<string, IServerCallHandler>();

        readonly TaskCompletionSource<object> shutdownTcs = new TaskCompletionSource<object>();

        static Server() {
            GrpcEnvironment.EnsureInitialized();
        }

        public Server()
        {
            // TODO: what is the tag for server shutdown?
            this.handle = ServerSafeHandle.NewServer(GetCompletionQueue(), IntPtr.Zero);
            this.newRpcHandler = HandleNewRpc;
            this.serverShutdownHandler = HandleServerShutdown;
        }

        // only call before Start(), this will be in server builder in the future.
        internal void AddCallHandler(string methodName, IServerCallHandler handler) {
            callHandlers.Add(methodName, handler);
        }
        // only call before Start()
        public int AddPort(string addr) {
            return handle.AddPort(addr);
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

                Console.WriteLine("Server received RPC " + rpcInfo.Method);

                IServerCallHandler callHandler;
                if (!callHandlers.TryGetValue(rpcInfo.Method, out callHandler))
                {
                    callHandler = new NoSuchMethodCallHandler();
                } 
                callHandler.StartCall(rpcInfo.Method, rpcInfo.Call, GetCompletionQueue());
            }
            catch(Exception e)
            {
                Console.WriteLine("Exception while handling RPC: " + e);
            }
        }

        /// <summary>
        /// Requests server shutdown and when there are no more calls being serviced,
        /// cleans up used resources.
        /// </summary>
        /// <returns>The async.</returns>
        public async Task ShutdownAsync() {
            handle.ShutdownAndNotify(serverShutdownHandler);
            await shutdownTcs.Task;
            handle.Dispose();
        }

        public void Kill() {
            handle.Dispose();
        }

        private async Task StartHandlingRpcs() {
            while (true)
            {
                await Task.Factory.StartNew(RunRpc);
            }
        }

        private void AllowOneRpc()
        {
            AssertCallOk(handle.RequestCall(newRpcHandler));
        }

        private void HandleNewRpc(IntPtr eventPtr)
        {
            try
            {
                var ev = new EventSafeHandleNotOwned(eventPtr);
                newRpcQueue.Add(new NewRpcInfo(ev.GetCall(), ev.GetServerRpcNewMethod()));
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