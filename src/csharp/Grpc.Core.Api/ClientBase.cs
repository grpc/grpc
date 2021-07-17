#region Copyright notice and license

// Copyright 2015-2016 gRPC authors.
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

using System;
using Grpc.Core.Interceptors;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Generic base class for client-side stubs.
    /// </summary>
    public abstract class ClientBase<T> : ClientBase
        where T : ClientBase<T>
    {
        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class that
        /// throws <c>NotImplementedException</c> upon invocation of any RPC.
        /// This constructor is only provided to allow creation of test doubles
        /// for client classes (e.g. mocking requires a parameterless constructor).
        /// </summary>
        protected ClientBase() : base()
        {
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="configuration">The configuration.</param>
        protected ClientBase(ClientBaseConfiguration configuration) : base(configuration)
        {
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="channel">The channel to use for remote call invocation.</param>
        public ClientBase(ChannelBase channel) : base(channel)
        {
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="callInvoker">The <c>CallInvoker</c> for remote call invocation.</param>
        public ClientBase(CallInvoker callInvoker) : base(callInvoker)
        {
        }

        /// <summary>
        /// Creates a new client that sets host field for calls explicitly.
        /// gRPC supports multiple "hosts" being served by a single server.
        /// By default (if a client was not created by calling this method),
        /// host <c>null</c> with the meaning "use default host" is used.
        /// </summary>
        public T WithHost(string host)
        {
            var newConfiguration = this.Configuration.WithHost(host);
            return NewInstance(newConfiguration);
        }

        /// <summary>
        /// Creates a new instance of client from given <c>ClientBaseConfiguration</c>.
        /// </summary>
        protected abstract T NewInstance(ClientBaseConfiguration configuration);
    }

    /// <summary>
    /// Base class for client-side stubs.
    /// </summary>
    public abstract class ClientBase
    {
        readonly ClientBaseConfiguration configuration;
        readonly CallInvoker callInvoker;

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class that
        /// throws <c>NotImplementedException</c> upon invocation of any RPC.
        /// This constructor is only provided to allow creation of test doubles
        /// for client classes (e.g. mocking requires a parameterless constructor).
        /// </summary>
        protected ClientBase() : this(new UnimplementedCallInvoker())
        {
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="configuration">The configuration.</param>
        protected ClientBase(ClientBaseConfiguration configuration)
        {
            this.configuration = GrpcPreconditions.CheckNotNull(configuration, "configuration");
            this.callInvoker = configuration.CreateDecoratedCallInvoker();
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="channel">The channel to use for remote call invocation.</param>
        public ClientBase(ChannelBase channel) : this(channel.CreateCallInvoker())
        {
        }

        /// <summary>
        /// Initializes a new instance of <c>ClientBase</c> class.
        /// </summary>
        /// <param name="callInvoker">The <c>CallInvoker</c> for remote call invocation.</param>
        public ClientBase(CallInvoker callInvoker) : this(new ClientBaseConfiguration(callInvoker, null))
        {
        }

        /// <summary>
        /// Gets the call invoker.
        /// </summary>
        protected CallInvoker CallInvoker
        {
            get { return this.callInvoker; }
        }

        /// <summary>
        /// Gets the configuration.
        /// </summary>
        internal ClientBaseConfiguration Configuration
        {
            get { return this.configuration; }
        }

        /// <summary>
        /// Represents configuration of ClientBase. The class itself is visible to
        /// subclasses, but contents are marked as internal to make the instances opaque.
        /// The verbose name of this class was chosen to make name clash in generated code 
        /// less likely.
        /// </summary>
        protected internal class ClientBaseConfiguration
        {
            private class ClientBaseConfigurationInterceptor : Interceptor
            {
                readonly Func<IMethod, string, CallOptions, ClientBaseConfigurationInfo> interceptor;

                /// <summary>
                /// Creates a new instance of ClientBaseConfigurationInterceptor given the specified header and host interceptor function.
                /// </summary>
                public ClientBaseConfigurationInterceptor(Func<IMethod, string, CallOptions, ClientBaseConfigurationInfo> interceptor)
                {
                    this.interceptor = GrpcPreconditions.CheckNotNull(interceptor, nameof(interceptor));
                }

                private ClientInterceptorContext<TRequest, TResponse> GetNewContext<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context)
                    where TRequest : class
                    where TResponse : class
                {
                    var newHostAndCallOptions = interceptor(context.Method, context.Host, context.Options);
                    return new ClientInterceptorContext<TRequest, TResponse>(context.Method, newHostAndCallOptions.Host, newHostAndCallOptions.CallOptions);
                }

                public override TResponse BlockingUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, BlockingUnaryCallContinuation<TRequest, TResponse> continuation)
                {
                    return continuation(request, GetNewContext(context));
                }

                public override AsyncUnaryCall<TResponse> AsyncUnaryCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, AsyncUnaryCallContinuation<TRequest, TResponse> continuation)
                {
                    return continuation(request, GetNewContext(context));
                }

                public override AsyncServerStreamingCall<TResponse> AsyncServerStreamingCall<TRequest, TResponse>(TRequest request, ClientInterceptorContext<TRequest, TResponse> context, AsyncServerStreamingCallContinuation<TRequest, TResponse> continuation)
                {
                    return continuation(request, GetNewContext(context));
                }

                public override AsyncClientStreamingCall<TRequest, TResponse> AsyncClientStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncClientStreamingCallContinuation<TRequest, TResponse> continuation)
                {
                    return continuation(GetNewContext(context));
                }

                public override AsyncDuplexStreamingCall<TRequest, TResponse> AsyncDuplexStreamingCall<TRequest, TResponse>(ClientInterceptorContext<TRequest, TResponse> context, AsyncDuplexStreamingCallContinuation<TRequest, TResponse> continuation)
                {
                    return continuation(GetNewContext(context));
                }
            }

            internal struct ClientBaseConfigurationInfo
            {
                internal readonly string Host;
                internal readonly CallOptions CallOptions;

                internal ClientBaseConfigurationInfo(string host, CallOptions callOptions)
                {
                    Host = host;
                    CallOptions = callOptions;
                }
            }

            readonly CallInvoker undecoratedCallInvoker;
            readonly string host;

            internal ClientBaseConfiguration(CallInvoker undecoratedCallInvoker, string host)
            {
                this.undecoratedCallInvoker = GrpcPreconditions.CheckNotNull(undecoratedCallInvoker);
                this.host = host;
            }

            internal CallInvoker CreateDecoratedCallInvoker()
            {
                return undecoratedCallInvoker.Intercept(new ClientBaseConfigurationInterceptor((method, host, options) => new ClientBaseConfigurationInfo(this.host, options)));
            }

            internal ClientBaseConfiguration WithHost(string host)
            {
                GrpcPreconditions.CheckNotNull(host, nameof(host));
                return new ClientBaseConfiguration(this.undecoratedCallInvoker, host);
            }
        }
    }
}
