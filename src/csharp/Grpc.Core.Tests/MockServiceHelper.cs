#region Copyright notice and license

// Copyright 2015 gRPC authors.
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
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    /// <summary>
    /// Allows setting up a mock service in the client-server tests easily.
    /// </summary>

    // note: historically all uses were string, but we can generalize that
    public class MockServiceHelper : MockServiceHelper<string>
    {
        public MockServiceHelper(string host = null, Marshaller<string> marshaller = null, IEnumerable<ChannelOption> channelOptions = null)
            : base(host, marshaller ?? Marshallers.StringMarshaller, channelOptions)
        { }

        public override string DefaultValue => "";
    }

    public class MockServiceHelperWrappedString : MockServiceHelper<WrappedString>
    {
        public MockServiceHelperWrappedString(string host = null, Marshaller<WrappedString> marshaller = null, IEnumerable<ChannelOption> channelOptions = null)
            : base(host, marshaller ?? Marshaller, channelOptions)
        { }

        public static readonly Marshaller<WrappedString> Marshaller = new Marshaller<WrappedString>(
            (WrappedString s) => System.Text.Encoding.UTF8.GetBytes(s.Value),
            (byte[] a) => System.Text.Encoding.UTF8.GetString(a));

        public override WrappedString DefaultValue => "";
    }

    public readonly struct WrappedString // allows us to test value-typed marshellers using existing tests which expect string-like semantics
    {
        public string Value { get; }
        public WrappedString(string value) => Value = value;
        public override string ToString() => $"[{Value}]";
        public override bool Equals(object obj) => throw new NotImplementedException("should unwrap");
        public override int GetHashCode() => throw new NotImplementedException("should unwrap");

        public static implicit operator string(WrappedString value) => value.Value;
        public static implicit operator WrappedString(string value) => new WrappedString(value);
    }


    public abstract class MockServiceHelper<T>
    {
        public const string ServiceName = "tests.Test";

        public virtual T DefaultValue => default(T);

        readonly string host;
        readonly IEnumerable<ChannelOption> channelOptions;

        readonly Method<T, T> unaryMethod;
        readonly Method<T, T> clientStreamingMethod;
        readonly Method<T, T> serverStreamingMethod;
        readonly Method<T, T> duplexStreamingMethod;

        UnaryServerMethod<T, T> unaryHandler;
        ClientStreamingServerMethod<T, T> clientStreamingHandler;
        ServerStreamingServerMethod<T, T> serverStreamingHandler;
        DuplexStreamingServerMethod<T, T> duplexStreamingHandler;

        Server server;
        Channel channel;

        public MockServiceHelper(string host = null, Marshaller<T> marshaller = null, IEnumerable<ChannelOption> channelOptions = null)
        {
            this.host = host ?? "localhost";
            this.channelOptions = channelOptions;

            unaryMethod = new Method<T, T>(
                MethodType.Unary,
                ServiceName,
                "Unary",
                marshaller,
                marshaller);

            clientStreamingMethod = new Method<T, T>(
                MethodType.ClientStreaming,
                ServiceName,
                "ClientStreaming",
                marshaller,
                marshaller);

            serverStreamingMethod = new Method<T, T>(
                MethodType.ServerStreaming,
                ServiceName,
                "ServerStreaming",
                marshaller,
                marshaller);

            duplexStreamingMethod = new Method<T, T>(
                MethodType.DuplexStreaming,
                ServiceName,
                "DuplexStreaming",
                marshaller,
                marshaller);

            ServiceDefinition = ServerServiceDefinition.CreateBuilder()
                .AddMethod(unaryMethod, (request, context) => unaryHandler(request, context))
                .AddMethod(clientStreamingMethod, (requestStream, context) => clientStreamingHandler(requestStream, context))
                .AddMethod(serverStreamingMethod, (request, responseStream, context) => serverStreamingHandler(request, responseStream, context))
                .AddMethod(duplexStreamingMethod, (requestStream, responseStream, context) => duplexStreamingHandler(requestStream, responseStream, context))
                .Build();

            var defaultStatus = new Status(StatusCode.Unknown, "Default mock implementation. Please provide your own.");

            unaryHandler = new UnaryServerMethod<T, T>((request, context) =>
            {
                context.Status = defaultStatus;
                return Task.FromResult(DefaultValue);
            });

            clientStreamingHandler = new ClientStreamingServerMethod<T, T>((requestStream, context) =>
            {
                context.Status = defaultStatus;
                return Task.FromResult(DefaultValue);
            });

            serverStreamingHandler = new ServerStreamingServerMethod<T, T>((request, responseStream, context) =>
            {
                context.Status = defaultStatus;
                return TaskUtils.CompletedTask;
            });

            duplexStreamingHandler = new DuplexStreamingServerMethod<T, T>((requestStream, responseStream, context) =>
            {
                context.Status = defaultStatus;
                return TaskUtils.CompletedTask;
            });
        }

        /// <summary>
        /// Returns the default server for this service and creates one if not yet created.
        /// </summary>
        public Server GetServer()
        {
            if (server == null)
            {
                // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
                server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
                {
                    Services = { ServiceDefinition },
                    Ports = { { Host, ServerPort.PickUnused, ServerCredentials.Insecure } }
                };
            }
            return server;
        }

        /// <summary>
        /// Returns the default channel for this service and creates one if not yet created.
        /// </summary>
        public Channel GetChannel()
        {
            if (channel == null)
            {
                channel = new Channel(Host, GetServer().Ports.Single().BoundPort, ChannelCredentials.Insecure, channelOptions);
            }
            return channel;
        }

        public CallInvocationDetails<T, T> CreateUnaryCall(CallOptions options = default(CallOptions))
        {
            return new CallInvocationDetails<T, T>(channel, unaryMethod, options);
        }

        public CallInvocationDetails<T, T> CreateClientStreamingCall(CallOptions options = default(CallOptions))
        {
            return new CallInvocationDetails<T, T>(channel, clientStreamingMethod, options);
        }

        public CallInvocationDetails<T, T> CreateServerStreamingCall(CallOptions options = default(CallOptions))
        {
            return new CallInvocationDetails<T, T>(channel, serverStreamingMethod, options);
        }

        public CallInvocationDetails<T, T> CreateDuplexStreamingCall(CallOptions options = default(CallOptions))
        {
            return new CallInvocationDetails<T, T>(channel, duplexStreamingMethod, options);
        }

        public string Host
        {
            get
            {
                return this.host;
            }
        }

        public ServerServiceDefinition ServiceDefinition { get; set; }
      
        public UnaryServerMethod<T, T> UnaryHandler
        {
            get
            {
                return this.unaryHandler;
            }

            set
            {
                unaryHandler = value;
            }
        }

        public ClientStreamingServerMethod<T, T> ClientStreamingHandler
        {
            get
            {
                return this.clientStreamingHandler;
            }

            set
            {
                clientStreamingHandler = value;
            }
        }

        public ServerStreamingServerMethod<T, T> ServerStreamingHandler
        {
            get
            {
                return this.serverStreamingHandler;
            }

            set
            {
                serverStreamingHandler = value;
            }
        }

        public DuplexStreamingServerMethod<T, T> DuplexStreamingHandler
        {
            get
            {
                return this.duplexStreamingHandler;
            }

            set
            {
                duplexStreamingHandler = value;
            }
        }
    }
}
