using System;

namespace Google.GRPC.Core
{
    // TODO: perhaps add also serverSideStreaming and clientSideStreaming

    public delegate void UnaryRequestServerMethod<TRequest, TResponse> (TRequest request, IObserver<TResponse> responseObserver);

    public delegate IObserver<TRequest> StreamingRequestServerMethod<TRequest, TResponse> (IObserver<TResponse> responseObserver);

    internal static class ServerCalls {

        public static IServerCallHandler UnaryRequestCall<TRequest, TResponse>(Method<TRequest, TResponse> method, UnaryRequestServerMethod<TRequest, TResponse> handler)
        {
            return new UnaryRequestServerCallHandler<TRequest, TResponse>(method, handler);
        }

        public static IServerCallHandler StreamingRequestCall<TRequest, TResponse>(Method<TRequest, TResponse> method, StreamingRequestServerMethod<TRequest, TResponse> handler)
        {
            return new StreamingRequestServerCallHandler<TRequest, TResponse>(method, handler);
        }

    }
}

