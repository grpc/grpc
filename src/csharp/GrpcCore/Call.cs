using System;
using Google.GRPC.Core.Internal;

namespace Google.GRPC.Core
{
    public class Call<TRequest, TResponse>
    {
        readonly string methodName;
        readonly Func<TRequest, byte[]> requestSerializer;
        readonly Func<byte[], TResponse> responseDeserializer;
        readonly Channel channel;

        public Call(string methodName, 
                    Func<TRequest, byte[]> requestSerializer,
                    Func<byte[], TResponse> responseDeserializer,
                    TimeSpan timeout,
                    Channel channel) {
            this.methodName = methodName;
            this.requestSerializer = requestSerializer;
            this.responseDeserializer = responseDeserializer;
            this.channel = channel;
        }

        public Call(Method<TRequest, TResponse> method, Channel channel)
        {
            this.methodName = method.Name;
            this.requestSerializer = method.RequestMarshaller.Serialize;
            this.responseDeserializer = method.ResponseMarshaller.Deserialize;
            this.channel = channel;
        }

        public Channel Channel
        {
            get
            {
                return this.channel;
            }
        }

        public string MethodName
        {
            get
            {
                return this.methodName;
            }
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
    }
}

