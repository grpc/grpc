using System;
using Google.GRPC.Wrappers;

namespace Google.GRPC.Core
{
    public class Call<TRequest, TResponse>
    {
        readonly string methodName;
        readonly Func<TRequest, byte[]> requestSerializer;
        readonly Func<byte[], TResponse> responseDeserializer;
        readonly Channel channel;

        // TODO: channel param should be removed in the future.
        public Call(string methodName, 
                    Func<TRequest, byte[]> requestSerializer,
                    Func<byte[], TResponse> responseDeserializer,
                    Channel channel) {
            this.methodName = methodName;
            this.requestSerializer = requestSerializer;
            this.responseDeserializer = responseDeserializer;
            this.channel = channel;
        }

        public CallContext CreateCallContext() {
            return channel.CreateCallContext(methodName);
        }

        public Func<TRequest, byte[]> RequestSerializer
        {
            get
            {
                return this.requestSerializer;
            }
        }

        public Func<byte[], TResponse> ResponseDeserializer
        {
            get
            {
                return this.responseDeserializer;
            }
        }

        public TResponse DeserializeResponse(byte[] payload) {
            return responseDeserializer(payload);
        }

        public byte[] SerializeRequest(TRequest req) {
            return requestSerializer(req);
        }
    }
}

