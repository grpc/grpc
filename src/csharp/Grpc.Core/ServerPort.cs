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

using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// A port exposed by a server.
    /// </summary>
    public class ServerPort
    {
        /// <summary>
        /// Pass this value as port to have the server choose an unused listening port for you.
        /// Ports added to a server will contain the bound port in their <see cref="BoundPort"/> property.
        /// </summary>
        public const int PickUnused = 0;

        readonly string host;
        readonly int port;
        readonly ServerCredentials credentials;
        readonly int boundPort;

        /// <summary>
        /// Creates a new port on which server should listen.
        /// </summary>
        /// <returns>The port on which server will be listening.</returns>
        /// <param name="host">the host</param>
        /// <param name="port">the port. If zero, an unused port is chosen automatically.</param>
        /// <param name="credentials">credentials to use to secure this port.</param>
        public ServerPort(string host, int port, ServerCredentials credentials)
        {
            this.host = GrpcPreconditions.CheckNotNull(host, "host");
            this.port = port;
            this.credentials = GrpcPreconditions.CheckNotNull(credentials, "credentials");
        }

        /// <summary>
        /// Creates a port from an existing <c>ServerPort</c> instance and boundPort value.
        /// </summary>
        internal ServerPort(ServerPort serverPort, int boundPort)
        {
            this.host = serverPort.host;
            this.port = serverPort.port;
            this.credentials = serverPort.credentials;
            this.boundPort = boundPort;
        }

        /// <value>The host.</value>
        public string Host
        {
            get
            {
                return host;
            }
        }

        /// <value>The port.</value>
        public int Port
        {
            get
            {
                return port;
            }
        }

        /// <value>The server credentials.</value>
        public ServerCredentials Credentials
        {
            get
            {
                return credentials;
            }
        }

        /// <value>
        /// The port actually bound by the server. This is useful if you let server
        /// pick port automatically. <see cref="PickUnused"/>
        /// </value>
        public int BoundPort
        {
            get
            {
                return boundPort;
            }
        }
    }
}
