using System;
using Google.GRPC.Core.Internal;

namespace Google.GRPC.Core
{
    internal interface IServerCallHandler
    {
        void StartCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq);
    }

    internal class UnaryRequestServerCallHandler<TRequest, TResponse> : IServerCallHandler
    {
        readonly Method<TRequest, TResponse> method;
        readonly UnaryRequestServerMethod<TRequest, TResponse> handler;

        public UnaryRequestServerCallHandler(Method<TRequest, TResponse> method, UnaryRequestServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public void StartCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCall<TResponse, TRequest>(
                (msg) => method.ResponseMarshaller.Serialize(msg),
                (payload) => method.RequestMarshaller.Deserialize(payload));

            asyncCall.InitializeServer(call);
            asyncCall.Accept(cq);
           
            var request = asyncCall.ReadAsync().Result;

            var responseObserver = new ServerWritingObserver<TResponse, TRequest>(asyncCall);
            handler(request, responseObserver);

            asyncCall.Halfclosed.Wait();
            // TODO: wait until writing is finished

            asyncCall.WriteStatusAsync(new Status(StatusCode.GRPC_STATUS_OK, "")).Wait();
            asyncCall.Finished.Wait();
        }
    }

    internal class StreamingRequestServerCallHandler<TRequest, TResponse> : IServerCallHandler
    {
        readonly Method<TRequest, TResponse> method;
        readonly StreamingRequestServerMethod<TRequest, TResponse> handler;

        public StreamingRequestServerCallHandler(Method<TRequest, TResponse> method, StreamingRequestServerMethod<TRequest, TResponse> handler)
        {
            this.method = method;
            this.handler = handler;
        }

        public void StartCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            var asyncCall = new AsyncCall<TResponse, TRequest>(
                (msg) => method.ResponseMarshaller.Serialize(msg),
                (payload) => method.RequestMarshaller.Deserialize(payload));

            asyncCall.InitializeServer(call);
            asyncCall.Accept(cq);

            var responseObserver = new ServerWritingObserver<TResponse, TRequest>(asyncCall);
            var requestObserver = handler(responseObserver);

            // feed the requests
            asyncCall.StartReadingToStream(requestObserver);

            asyncCall.Halfclosed.Wait();

            asyncCall.WriteStatusAsync(new Status(StatusCode.GRPC_STATUS_OK, "")).Wait();
            asyncCall.Finished.Wait();
        }
    }

    internal class NoSuchMethodCallHandler : IServerCallHandler
    {
        public void StartCall(string methodName, CallSafeHandle call, CompletionQueueSafeHandle cq)
        {
            // We don't care about the payload type here.
            AsyncCall<byte[], byte[]> asyncCall = new AsyncCall<byte[], byte[]>(
                (payload) => payload, (payload) => payload);

            asyncCall.InitializeServer(call);
            asyncCall.Accept(cq);
            asyncCall.WriteStatusAsync(new Status(StatusCode.GRPC_STATUS_UNIMPLEMENTED, "No such method.")).Wait();

            asyncCall.Finished.Wait();
        }
    }
}

