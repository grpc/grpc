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
using System.Collections.Immutable;
using Grpc.Core.Internal;

namespace Grpc.Core
{
    /// <summary>
    /// Server side credentials.
    /// </summary>
    public abstract class ServerCredentials
    {
        /// <summary>
        /// Creates native object for the credentials.
        /// </summary>
        /// <returns>The native credentials.</returns>
        internal abstract ServerCredentialsSafeHandle ToNativeCredentials();
    }

    /// <summary>
    /// Key certificate pair (in PEM encoding).
    /// </summary>
    public class KeyCertificatePair
    {
        readonly string certChain;
        readonly string privateKey;

        public KeyCertificatePair(string certChain, string privateKey)
        {
            this.certChain = certChain;
            this.privateKey = privateKey;
        }

        public string CertChain
        {
            get
            {
                return certChain;
            }
        }

        public string PrivateKey
        {
            get
            {
                return privateKey;
            }
        }
    }

    /// <summary>
    /// Server-side SSL credentials.
    /// </summary>
    public class SslServerCredentials : ServerCredentials
    {
        ImmutableList<KeyCertificatePair> keyCertPairs;

        public SslServerCredentials(ImmutableList<KeyCertificatePair> keyCertPairs)
        {
            this.keyCertPairs = keyCertPairs;
        }

        internal override ServerCredentialsSafeHandle ToNativeCredentials()
        {
            int count = keyCertPairs.Count;
            string[] certChains = new string[count];
            string[] keys = new string[count];
            for (int i = 0; i < count; i++)
            {
                certChains[i] = keyCertPairs[i].CertChain;
                keys[i] = keyCertPairs[i].PrivateKey;
            }
            return ServerCredentialsSafeHandle.CreateSslCredentials(certChains, keys);
        }
    }
}
