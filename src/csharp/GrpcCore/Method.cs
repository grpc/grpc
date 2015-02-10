using System;

namespace Google.GRPC.Core
{
    public enum MethodType
    {
        Unary,
        ClientStreaming,
        ServerStreaming,
        DuplexStreaming
    }

    /// <summary>
    /// A description of a service method.
    /// </summary>
    public class Method<TRequest, TResponse>
    {
        readonly MethodType type;
        readonly string name;
        readonly Marshaller<TRequest> requestMarshaller;
        readonly Marshaller<TResponse> responseMarshaller;

        public Method(MethodType type, string name, Marshaller<TRequest> requestMarshaller, Marshaller<TResponse> responseMarshaller)
        {
            this.type = type;
            this.name = name;
            this.requestMarshaller = requestMarshaller;
            this.responseMarshaller = responseMarshaller;
        }

        public MethodType Type
        {
            get
            {
                return this.type;
            }
        }

        public string Name
        {
            get
            {
                return this.name;
            }
        }

        public Marshaller<TRequest> RequestMarshaller
        {
            get
            {
                return this.requestMarshaller;
            }
        }

        public Marshaller<TResponse> ResponseMarshaller
        {
            get
            {
                return this.responseMarshaller;
            }
        }
    }
}

