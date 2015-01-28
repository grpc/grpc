using System;
using System.Threading;
using System.Threading.Tasks;
using Google.GRPC.Wrappers;

namespace Google.GRPC.Core
{
    /// <summary>
    /// Helper methods for generated stubs to make RPC calls.
    /// </summary>
    public static class Calls
    {
        public static TResponse BlockingUnaryCall<TRequest, TResponse>(Call<TRequest, TResponse> call, TRequest req, CancellationToken token)
        {
            using (CallContext ctx = call.CreateCallContext())
            {
                ctx.Start(false);

                // TODO: handle the result
                WriteUnaryRequest(ctx, req, call.RequestSerializer);

                return ReadUnaryResponseAndWait(ctx, call.ResponseDeserializer);
            }
        }

        public static Task<TResponse> AsyncUnaryCall<TRequest, TResponse>(Call<TRequest, TResponse> call, TRequest req, CancellationToken token)
        {
            throw new InvalidOperationException("Not implemented");
        }


        public static IObservable<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, TRequest req, CancellationToken token)
        {
            using (CallContext ctx = call.CreateCallContext())
            {
                ctx.Start(false);

                // TODO: handle the result
                WriteUnaryRequest(ctx, req, call.SerializeRequest);

                return new StreamingOutputObservable<TResponse>(ctx.AddRef(), call.ResponseDeserializer);
            }

        }

        public static Task<TResponse> AsyncClientStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, IObservable<TRequest> inputs, CancellationToken token)
        {
            using (CallContext ctx = call.CreateCallContext())
            {
                ctx.Start(false);

                //  TODO: dispose the subscription....
                IDisposable subscription = inputs.Subscribe(new StreamingInputObserver<TRequest>(ctx.AddRef(), call.RequestSerializer));

                ICallContext ctxRef = ctx.AddRef();
                return Task<TResponse>.Factory.StartNew(
                    () => {
                        try
                        {
                            return ReadUnaryResponseAndWait(ctxRef, call.ResponseDeserializer);
                        } 
                        finally
                        {
                            ctxRef.Dispose();
                        }
                });
            }
        }

        public static TResponse BlockingClientStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, IObservable<TRequest> inputs, CancellationToken token)
        {
            throw new InvalidOperationException("Not implemented");
        }

        public static IObservable<TResponse> DuplexStreamingCall<TRequest, TResponse>(Call<TRequest, TResponse> call, IObservable<TRequest> inputs, CancellationToken token)
        {
            using (CallContext ctx = call.CreateCallContext())
            {
                ctx.Start(false);

                //  TODO: dispose the subscription....
                IDisposable subscription = inputs.Subscribe(new StreamingInputObserver<TRequest>(ctx.AddRef(), call.RequestSerializer));

                return new StreamingOutputObservable<TResponse>(ctx.AddRef(), call.DeserializeResponse);
            }
        }

        private static bool WriteUnaryRequest<TRequest>(ICallContext ctx, TRequest req, Func<TRequest, byte[]> serializer) {

            // TODO: handle serialize error...
            byte[] requestPayload = serializer(req);
            if (!ctx.Write(requestPayload))
            {
                // TODO: throw some meaningful exception...
                throw new Exception();
            }
            ctx.WritesDone();
            return true;
        }

        private static TResponse ReadUnaryResponseAndWait<TResponse>(ICallContext ctx, Func<byte[], TResponse> deserializer) {
            // TODO: handle read errors
            byte[] resultPayload = ctx.Read();

            // TODO: handle deserialization errors
            TResponse response = deserializer(resultPayload);

            Status status = ctx.Wait();
            if (status.StatusCode != StatusCode.GRPC_STATUS_OK)
            {
                throw new RpcException(status);
            }
            return response;
        }
    }
}

