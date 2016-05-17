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
