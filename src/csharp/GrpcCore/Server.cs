using System;
using System.Runtime.InteropServices;
using System.Diagnostics;
using System.Collections.Concurrent;
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

        readonly BlockingCollection<NewRpcInfo> newRpcQueue = new BlockingCollection<NewRpcInfo>();
        readonly ServerSafeHandle handle;

        static Server() {
            GrpcEnvironment.EnsureInitialized();
        }

        public Server()
        {
            // TODO: what is the tag for server shutdown?
            this.handle = ServerSafeHandle.NewServer(GetCompletionQueue(), IntPtr.Zero);
            this.newRpcHandler = HandleNewRpc;
        }

        public int AddPort(string addr) {
            return handle.AddPort(addr);
        }

        public void Start()
        {
            handle.Start();
        }

        public void RunRpc()
        {
            AllowOneRpc();
         
            try {
            var rpcInfo = newRpcQueue.Take();

            Console.WriteLine("Server received RPC " + rpcInfo.Method);

            AsyncCall<byte[], byte[]> asyncCall = new AsyncCall<byte[], byte[]>(
                (payload) => payload, (payload) => payload);

            asyncCall.InitializeServer(rpcInfo.Call);

            asyncCall.Accept(GetCompletionQueue());

            while(true) {
                byte[] payload = asyncCall.ReadAsync().Result;
                if (payload == null)
                {
                    break;
                }
            }

            asyncCall.WriteAsync(new byte[] { }).Wait();

            // TODO: what should be the details?
            asyncCall.WriteStatusAsync(new Status(StatusCode.GRPC_STATUS_OK, "")).Wait();

            asyncCall.Finished.Wait();
            } catch(Exception e) {
                Console.WriteLine("Exception while handling RPC: " + e);
            }
        }

        // TODO: implement disposal properly...
        public void Shutdown() {
            handle.Shutdown();


            //handle.Dispose();
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