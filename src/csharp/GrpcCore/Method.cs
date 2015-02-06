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
        readonly IMarshaller<TRequest> requestMarshaller;
        readonly IMarshaller<TResponse> responseMarshaller;

        public Method(MethodType type, string name, IMarshaller<TRequest> requestMarshaller, IMarshaller<TResponse> responseMarshaller)
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

        public IMarshaller<TRequest> RequestMarshaller
        {
            get
            {
                return this.requestMarshaller;
            }
        }

        public IMarshaller<TResponse> ResponseMarshaller
        {
            get
            {
                return this.responseMarshaller;
            }
        }
    }
}

