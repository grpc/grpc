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

using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Key certificate pair (in PEM encoding).
    /// </summary>
    public sealed class KeyCertificatePair
    {
        readonly string certificateChain;
        readonly string privateKey;

        /// <summary>
        /// Creates a new certificate chain - private key pair.
        /// </summary>
        /// <param name="certificateChain">PEM encoded certificate chain.</param>
        /// <param name="privateKey">PEM encoded private key.</param>
        public KeyCertificatePair(string certificateChain, string privateKey)
        {
            this.certificateChain = GrpcPreconditions.CheckNotNull(certificateChain, "certificateChain");
            this.privateKey = GrpcPreconditions.CheckNotNull(privateKey, "privateKey");
        }

        /// <summary>
        /// PEM encoded certificate chain.
        /// </summary>
        public string CertificateChain
        {
            get
            {
                return certificateChain;
            }
        }

        /// <summary>
        /// PEM encoded private key.
        /// </summary>
        public string PrivateKey
        {
            get
            {
                return privateKey;
            }
        }
    }
}
