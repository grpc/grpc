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
    /// Server side credentials.
    /// </summary>
    public abstract class ServerCredentials
    {
        static readonly ServerCredentials InsecureInstance = new InsecureServerCredentialsImpl();

        /// <summary>
        /// Returns instance of credential that provides no security and 
        /// will result in creating an unsecure server port with no encryption whatsoever.
        /// </summary>
        public static ServerCredentials Insecure
        {
            get
            {
                return InsecureInstance;
            }
        }

        /// <summary>
        /// Creates native object for the credentials.
        /// </summary>
        /// <returns>The native credentials.</returns>
        internal abstract ServerCredentialsSafeHandle ToNativeCredentials();

        private sealed class InsecureServerCredentialsImpl : ServerCredentials
        {
            internal override ServerCredentialsSafeHandle ToNativeCredentials()
            {
                return null;
            }
        }
    }

    /// <summary>
    /// Server-side SSL credentials.
    /// </summary>
    public class SslServerCredentials : ServerCredentials
    {
        readonly IList<KeyCertificatePair> keyCertificatePairs;
        readonly string rootCertificates;
        readonly bool forceClientAuth;

        /// <summary>
        /// Creates server-side SSL credentials.
        /// </summary>
        /// <param name="keyCertificatePairs">Key-certificates to use.</param>
        /// <param name="rootCertificates">PEM encoded client root certificates used to authenticate client.</param>
        /// <param name="forceClientAuth">If true, client will be rejected unless it proves its unthenticity using against rootCertificates.</param>
        public SslServerCredentials(IEnumerable<KeyCertificatePair> keyCertificatePairs, string rootCertificates, bool forceClientAuth)
        {
            this.keyCertificatePairs = new List<KeyCertificatePair>(keyCertificatePairs).AsReadOnly();
            GrpcPreconditions.CheckArgument(this.keyCertificatePairs.Count > 0,
                "At least one KeyCertificatePair needs to be provided.");
            if (forceClientAuth)
            {
                GrpcPreconditions.CheckNotNull(rootCertificates,
                    "Cannot force client authentication unless you provide rootCertificates.");
            }
            this.rootCertificates = rootCertificates;
            this.forceClientAuth = forceClientAuth;
        }

        /// <summary>
        /// Creates server-side SSL credentials.
        /// This constructor should be use if you do not wish to autheticate client
        /// using client root certificates.
        /// </summary>
        /// <param name="keyCertificatePairs">Key-certificates to use.</param>
        public SslServerCredentials(IEnumerable<KeyCertificatePair> keyCertificatePairs) : this(keyCertificatePairs, null, false)
        {
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

        /// <summary>
        /// If true, the authenticity of client check will be enforced.
        /// </summary>
        public bool ForceClientAuthentication
        {
            get
            {
                return this.forceClientAuth;
            }
        }

        internal override ServerCredentialsSafeHandle ToNativeCredentials()
        {
            int count = keyCertificatePairs.Count;
            string[] certChains = new string[count];
            string[] keys = new string[count];
            for (int i = 0; i < count; i++)
            {
                certChains[i] = keyCertificatePairs[i].CertificateChain;
                keys[i] = keyCertificatePairs[i].PrivateKey;
            }
            return ServerCredentialsSafeHandle.CreateSslCredentials(rootCertificates, certChains, keys, forceClientAuth);
        }
    }
}
