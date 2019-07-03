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
using Grpc.Core;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Details of a newly received RPC.
    /// </summary>
    internal struct ServerRpcNew
    {
        readonly Server server;
        readonly CallSafeHandle call;
        readonly StringLike method;
        readonly StringLike host;
        readonly Timespec deadline;
        readonly Metadata requestMetadata;

        public void Recycle()
        {
            method.Recycle();
            host.Recycle();
        }

        public ServerRpcNew(Server server, CallSafeHandle call, StringLike method, StringLike host, Timespec deadline, Metadata requestMetadata)
        {
            this.server = server;
            this.call = call;
            this.method = method;
            this.host = host;
            this.deadline = deadline;
            this.requestMetadata = requestMetadata;
        }

        public Server Server
        {
            get
            {
                return this.server;
            }
        }

        public CallSafeHandle Call
        {
            get
            {
                return this.call;
            }
        }

        public StringLike Method
        {
            get
            {
                return this.method;
            }
        }

        public StringLike Host
        {
            get
            {
                return this.host;
            }
        }

        public Timespec Deadline
        {
            get
            {
                return this.deadline;
            }
        }

        public Metadata RequestMetadata
        {
            get
            {
                return this.requestMetadata;
            }
        }

        internal ServerRpcNew WithMethod(StringLike method)
        {
            // create a new instance with the designated .Method, recycling
            // the old .Method since it is now toast
            this.method.Recycle();
            return new ServerRpcNew(server, call, method, host, deadline, requestMetadata);
        }
    }
}
