#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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
using System.Runtime.InteropServices;
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Type of "local" connections for which the local credentials will be applied.
    /// </summary>
    public enum LocalCredentialsType
    {
        /// <summary>
        /// Trusted connection over UDS
        /// </summary>
        UnixDomainSocket = 0,

        /// <summary>
        /// Trusted TCP connection to the local host
        /// </summary>
        LocalTcp
    }

    /// <summary>
    /// Credentials that allow sending sensitive metadata such
    /// as authentication tokens over a trusted local connection.
    /// Note: experimental API that can change or be removed without any prior notice.
    /// </summary>
    public sealed class LocalCredentials : ChannelCredentials
    {
        readonly LocalCredentialsType type;

        /// <summary>
        /// Initializes a new instance of <c>LocalCredentials</c> class.
        /// </summary>
        /// <param name="type">type of local connection accepted by the credentials</param>
        public LocalCredentials(LocalCredentialsType type)
        {
            this.type = type;
        }

        internal override ChannelCredentialsSafeHandle CreateNativeCredentials()
        {
            return ChannelCredentialsSafeHandle.CreateLocal(type);
        }
    }
}
