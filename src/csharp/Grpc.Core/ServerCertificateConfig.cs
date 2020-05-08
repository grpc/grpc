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
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Server certificate configuration encapsulates
    /// SSL server certificates, private keys, and trusted CAs.
    /// </summary>
    public class ServerCertificateConfig
    {
        readonly IList<KeyCertificatePair> keyCertificatePairs;
        readonly string rootCertificates;

        /// <summary>
        /// Creates a server certificate configuration.
        /// </summary>
        /// <param name="keyCertificatePairs">Key-certificates to use.</param>
        /// <param name="rootCertificates">PEM encoded client root certificates used to authenticate client.</param>
        public ServerCertificateConfig(IEnumerable<KeyCertificatePair> keyCertificatePairs, string rootCertificates)
        {
            this.keyCertificatePairs = new List<KeyCertificatePair>(keyCertificatePairs).AsReadOnly();
            GrpcPreconditions.CheckArgument(
                this.keyCertificatePairs.Count > 0,
                "At least one KeyCertificatePair needs to be provided.");
            this.rootCertificates = rootCertificates;
        }

        /// <summary>
        /// Key-certificate pairs.
        /// </summary>
        public IList<KeyCertificatePair> KeyCertificatePairs
        {
            get
            {
                return this.keyCertificatePairs;
            }
        }

        /// <summary>
        /// PEM encoded client root certificates.
        /// </summary>
        public string RootCertificates
        {
            get
            {
                return this.rootCertificates;
            }
        }

        internal ServerCertificateConfigSafeHandle ToNative()
        {
            int count = keyCertificatePairs.Count;
            string[] keyCertPairCertChainArray = new string[count];
            string[] keyCertPairPrivateKeyArray = new string[count];
            for (int i = 0; i < count; i++)
            {
                keyCertPairCertChainArray[i] = keyCertificatePairs[i].CertificateChain;
                keyCertPairPrivateKeyArray[i] = keyCertificatePairs[i].PrivateKey;
            }

            return ServerCertificateConfigSafeHandle.CreateSslServerCertificateConfig(
                rootCertificates,
                keyCertPairCertChainArray,
                keyCertPairPrivateKeyArray);
        }
    }
}
