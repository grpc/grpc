#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
                server = new Server
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
