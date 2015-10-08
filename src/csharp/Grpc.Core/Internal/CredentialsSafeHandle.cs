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
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_credentials from <c>grpc/grpc_security.h</c>
    /// </summary>
    internal class CredentialsSafeHandle : SafeHandleZeroIsInvalid
    {
        [DllImport("grpc_csharp_ext.dll", CharSet = CharSet.Ansi)]
        static extern CredentialsSafeHandle grpcsharp_ssl_credentials_create(string pemRootCerts, string keyCertPairCertChain, string keyCertPairPrivateKey);

        [DllImport("grpc_csharp_ext.dll")]
        static extern CredentialsSafeHandle grpcsharp_composite_credentials_create(CredentialsSafeHandle creds1, CredentialsSafeHandle creds2);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_credentials_release(IntPtr credentials);

        private CredentialsSafeHandle()
        {
        }

        public static CredentialsSafeHandle CreateNullCredentials()
        {
            var creds = new CredentialsSafeHandle();
            creds.SetHandle(IntPtr.Zero);
            return creds;
        }

        public static CredentialsSafeHandle CreateSslCredentials(string pemRootCerts, KeyCertificatePair keyCertPair)
        {
            if (keyCertPair != null)
            {
                return grpcsharp_ssl_credentials_create(pemRootCerts, keyCertPair.CertificateChain, keyCertPair.PrivateKey);
            }
            else
            {
                return grpcsharp_ssl_credentials_create(pemRootCerts, null, null);
            }
        }

        public static CredentialsSafeHandle CreateComposite(CredentialsSafeHandle creds1, CredentialsSafeHandle creds2)
        {
            return grpcsharp_composite_credentials_create(creds1, creds2);
        }

        protected override bool ReleaseHandle()
        {
            grpcsharp_credentials_release(handle);
            return true;
        }
    }
}
