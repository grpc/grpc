using System;
using System.Collections.Generic;

namespace Google.GRPC.Core
{
    public class ServerServiceDefinition
    {
        readonly string serviceName;
        // TODO: we would need an immutable dictionary here...
        readonly Dictionary<string, IServerCallHandler> callHandlers;

        private ServerServiceDefinition(string serviceName, Dictionary<string, IServerCallHandler> callHandlers)
        {
            this.serviceName = serviceName;
            this.callHandlers = new Dictionary<string, IServerCallHandler>(callHandlers);
        }

        internal Dictionary<string, IServerCallHandler> CallHandlers
        {
            get
            {
                return this.callHandlers;
            }
        }


        public static Builder CreateBuilder(String serviceName)
        {
            return new Builder(serviceName);
        }

        public class Builder
        {
            readonly string serviceName;
            readonly Dictionary<string, IServerCallHandler> callHandlers = new Dictionary<String, IServerCallHandler>();

            public Builder(string serviceName)
            {
                this.serviceName = serviceName;
            }

            public Builder AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method, 
                UnaryRequestServerMethod<TRequest, TResponse> handler)
            {
                callHandlers.Add(method.Name, ServerCalls.UnaryRequestCall(method, handler));
                return this;
            }

            public Builder AddMethod<TRequest, TResponse>(
                Method<TRequest, TResponse> method, 
                StreamingRequestServerMethod<TRequest, TResponse> handler)
            {
                callHandlers.Add(method.Name, ServerCalls.StreamingRequestCall(method, handler));
                return this;
            }

            public ServerServiceDefinition Build()
            {
                return new ServerServiceDefinition(serviceName, callHandlers);
            }
        }
    }
}

