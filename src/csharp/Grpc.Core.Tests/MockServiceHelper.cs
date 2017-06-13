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
    public class MockServiceHelper
    {
        public const string ServiceName = "tests.Test";

        readonly string host;
        readonly ServerServiceDefinition serviceDefinition;
        readonly IEnumerable<ChannelOption> channelOptions;

        readonly Method<string, string> unaryMethod;
        readonly Method<string, string> clientStreamingMethod;
        readonly Method<string, string> serverStreamingMethod;
        readonly Method<string, string> duplexStreamingMethod;

        UnaryServerMethod<string, string> unaryHandler;
        ClientStreamingServerMethod<string, string> clientStreamingHandler;
        ServerStreamingServerMethod<string, string> serverStreamingHandler;
        DuplexStreamingServerMethod<string, string> duplexStreamingHandler;

        Server server;
        Channel channel;

        public MockServiceHelper(string host = null, Marshaller<string> marshaller = null, IEnumerable<ChannelOption> channelOptions = null)
        {
            this.host = host ?? "localhost";
            this.channelOptions = channelOptions;
            marshaller = marshaller ?? Marshallers.StringMarshaller;

            unaryMethod = new Method<string, string>(
                MethodType.Unary,
                ServiceName,
                "Unary",
                marshaller,
                marshaller);

            clientStreamingMethod = new Method<string, string>(
                MethodType.ClientStreaming,
                ServiceName,
                "ClientStreaming",
                marshaller,
                marshaller);

            serverStreamingMethod = new Method<string, string>(
                MethodType.ServerStreaming,
                ServiceName,
                "ServerStreaming",
                marshaller,
                marshaller);

            duplexStreamingMethod = new Method<string, string>(
                MethodType.DuplexStreaming,
                ServiceName,
                "DuplexStreaming",
                marshaller,
                marshaller);

            serviceDefinition = ServerServiceDefinition.CreateBuilder()
                .AddMethod(unaryMethod, (request, context) => unaryHandler(request, context))
                .AddMethod(clientStreamingMethod, (requestStream, context) => clientStreamingHandler(requestStream, context))
                .AddMethod(serverStreamingMethod, (request, responseStream, context) => serverStreamingHandler(request, responseStream, context))
                .AddMethod(duplexStreamingMethod, (requestStream, responseStream, context) => duplexStreamingHandler(requestStream, responseStream, context))
                .Build();

            var defaultStatus = new Status(StatusCode.Unknown, "Default mock implementation. Please provide your own.");

            unaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                context.Status = defaultStatus;
                return "";
            });

            clientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                context.Status = defaultStatus;
                return "";
            });

            serverStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) =>
            {
                context.Status = defaultStatus;
            });

            duplexStreamingHandler = new DuplexStreamingServerMethod<string, string>(async (requestStream, responseStream, context) =>
            {
                context.Status = defaultStatus;
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
                    Services = { serviceDefinition },
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

        public CallInvocationDetails<string, string> CreateUnaryCall(CallOptions options = default(CallOptions))
        {
            return new CallInvocationDetails<string, string>(channel, unaryMethod, options);
        }

        public CallInvocationDetails<string, string> CreateClientStreamingCall(CallOptions options = default(CallOptions))
        {
            return new CallInvocationDetails<string, string>(channel, clientStreamingMethod, options);
        }

        public CallInvocationDetails<string, string> CreateServerStreamingCall(CallOptions options = default(CallOptions))
        {
            return new CallInvocationDetails<string, string>(channel, serverStreamingMethod, options);
        }

        public CallInvocationDetails<string, string> CreateDuplexStreamingCall(CallOptions options = default(CallOptions))
        {
            return new CallInvocationDetails<string, string>(channel, duplexStreamingMethod, options);
        }

        public string Host
        {
            get
            {
                return this.host;
            }
        }

        public ServerServiceDefinition ServiceDefinition
        {
            get
            {
                return this.serviceDefinition;
            }
        }
      
        public UnaryServerMethod<string, string> UnaryHandler
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

        public ClientStreamingServerMethod<string, string> ClientStreamingHandler
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

        public ServerStreamingServerMethod<string, string> ServerStreamingHandler
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

        public DuplexStreamingServerMethod<string, string> DuplexStreamingHandler
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
