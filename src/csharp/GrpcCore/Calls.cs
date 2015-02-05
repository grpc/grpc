using System;
using System.Threading;
using System.Threading.Tasks;
using Google.GRPC.Core.Internal;

namespace Google.GRPC.Core
{
    // NOTE: this class is work-in-progress

    /// <summary>
    /// Helper methods for generated stubs to make RPC calls.
    /// </summary>
    public static class Calls
    {
        public static TResponse BlockingUnaryCall<TRequest, TResponse>(Call<TRequest, TResponse> call, TRequest req, CancellationToken token)
        {
            //TODO: implement this in real synchronous style once new GRPC C core API is available.
            return AsyncUnaryCall(call, req, token).Result;
        }

        public static async Task<TResponse> AsyncUnaryCall<TRequest, TResponse>(Call<TRequest, TResponse> call, TRequest req, CancellationToken token)
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call.RequestSerializer, call.ResponseDeserializer);
            asyncCall.Initialize(call.Channel, call.MethodName);
            asyncCall.Start(false, GetCompletionQueue());

            await asyncCall.WriteAsync(req);
            await asyncCall.WritesCompletedAsync();

            TResponse response = await asyncCall.ReadAsync();

            Status status = await asyncCall.Finished;

            if (status.StatusCode != StatusCode.GRPC_STATUS_OK)
            {
                throw new RpcException(status);
            }
            return response;
        }

        public static async Task AsyncServerStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, TRequest req, IObserver<TResponse> outputs, CancellationToken token)
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call.RequestSerializer, call.ResponseDeserializer);
            asyncCall.Initialize(call.Channel, call.MethodName);
            asyncCall.Start(false, GetCompletionQueue());

            asyncCall.StartReadingToStream(outputs);

            await asyncCall.WriteAsync(req);
            await asyncCall.WritesCompletedAsync();
        }

        public static ClientStreamingAsyncResult<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, CancellationToken token)
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call.RequestSerializer, call.ResponseDeserializer);
            asyncCall.Initialize(call.Channel, call.MethodName);
            asyncCall.Start(false, GetCompletionQueue());

            var task = asyncCall.ReadAsync();
            var inputs = new StreamingInputObserver<TRequest, TResponse>(asyncCall);
            return new ClientStreamingAsyncResult<TRequest, TResponse>(task, inputs);
        }

        public static TResponse BlockingClientStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, IObservable<TRequest> inputs, CancellationToken token)
        {
            throw new NotImplementedException();
        }

        public static IObserver<TRequest> DuplexStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, IObserver<TResponse> outputs, CancellationToken token)
        {
            var asyncCall = new AsyncCall<TRequest, TResponse>(call.RequestSerializer, call.ResponseDeserializer);
            asyncCall.Initialize(call.Channel, call.MethodName);
            asyncCall.Start(false, GetCompletionQueue());

            asyncCall.StartReadingToStream(outputs);
            var inputs = new StreamingInputObserver<TRequest, TResponse>(asyncCall);
            return inputs;
        }

        private static CompletionQueueSafeHandle GetCompletionQueue() {
            return GrpcEnvironment.ThreadPool.CompletionQueue;
        }
    }
}

