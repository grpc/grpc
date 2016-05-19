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
        readonly string method;
        readonly string host;
        readonly Timespec deadline;
        readonly Metadata requestMetadata;

        public ServerRpcNew(Server server, CallSafeHandle call, string method, string host, Timespec deadline, Metadata requestMetadata)
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

        public string Method
        {
            get
            {
                return this.method;
            }
        }

        public string Host
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
    }
}
