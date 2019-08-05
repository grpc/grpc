#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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

using System.Threading.Tasks;
using BenchmarkDotNet.Attributes;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using System;

using System.Collections.Generic;

namespace Grpc.Microbenchmarks
{
    // this test measures the overhead of C# wrapping layer when requesting and dispatching
    // a new call on the server side
    [ClrJob, CoreJob]
    [MemoryDiagnoser]
    public class HandleServerCallOverheadBenchmark
    {
         Server server;
        readonly Stack<RequestedCallDetails> requestedCalls = new Stack<RequestedCallDetails>();

        [Benchmark]
        public void RequestAndDispatchRpcOverhead()
        { 
            var origRequestedCallCount = requestedCalls.Count;

            var requestedCall = requestedCalls.Pop();
            var completionCallback = requestedCall.completionRegistry.Extract(requestedCall.tag);
            // the completion for requested call extracts details of the call,
            // dispatches it and also submits a request for another call.
            completionCallback.OnComplete(true);

            // checking that a new call was requested is a good way of checking that
            // the benchmark is working as expected.
            GrpcPreconditions.CheckState(requestedCalls.Count == origRequestedCallCount);
        }

       
        [GlobalSetup]
        public void Setup()
        {
            GrpcEnvironment.SetCompletionQueueCount(1);
            var native = NativeMethods.Get();

            // replace the implementation of a native method with a fake
            NativeMethods.Delegates.grpcsharp_server_request_call_delegate fakeServerRequestCall = (ServerSafeHandle server, CompletionQueueSafeHandle cq, RequestCallContextSafeHandle ctx) => {
                var result = native.grpcsharp_test_server_request_call_fake(server, cq, ctx);
                requestedCalls.Push(new RequestedCallDetails(cq.CompletionRegistry, ctx.Handle));
                return result;
            };
            native.GetType().GetField(nameof(native.grpcsharp_server_request_call)).SetValue(native, fakeServerRequestCall);

            NativeMethods.Delegates.grpcsharp_call_destroy_delegate fakeCallDestroy = (IntPtr) =>
            {
                // just ignore, the call pointers returned by 
                // grpcsharp_test_server_request_call_fake are not real object.
            };
            native.GetType().GetField(nameof(native.grpcsharp_call_destroy)).SetValue(native, fakeCallDestroy);

            // create server, the listening port and address is not important as the logic for requesting
            // calls in short-circuted
            server = new Server
            {
                Services = { },
                Ports = { { "localhost", ServerPort.PickUnused, ServerCredentials.Insecure } }
            };
            var fakeCallHandler = new FakeCallHandler();
            // method name needs to match to whatever is produced by grpcsharp_test_server_request_call_fake
            server.AddCallHandlerInternal("/someservice/somemethod", fakeCallHandler);

            server.Start();
        }

        [GlobalCleanup]
        public async Task Cleanup()
        {
            // drain the requested calls to prevent getting a "pending completions" error at shutdown
            while (requestedCalls.Count > 0)
            {
                var requestedCall = requestedCalls.Pop();
                var completionCallback = requestedCall.completionRegistry.Extract(requestedCall.tag);
                
                // cleanup contents of ctx cleanly by setting callback to NOP
                // but still running OnComplete
                var ctx = (RequestCallContextSafeHandle) completionCallback;
                ctx.CompletionCallback = (success, _) => {};
                completionCallback.OnComplete(true);
            }
            await server.ShutdownAsync();
        }

        private class FakeCallHandler : IServerCallHandler
        {
            public Task HandleCall(ServerRpcNew newRpc, CompletionQueueSafeHandle cq)
            {
                // don't do anything, just return synchronously
                newRpc.Call.Dispose();  // prevent the call objects from accumulating.
                return Task.CompletedTask;
            }
        }

        private struct RequestedCallDetails
        {
            public RequestedCallDetails(CompletionRegistry completionRegistry, IntPtr tag)
            {
                this.completionRegistry = completionRegistry;
                this.tag = tag;
            }
            public readonly CompletionRegistry completionRegistry;
            public readonly IntPtr tag;
        }
    }
}
