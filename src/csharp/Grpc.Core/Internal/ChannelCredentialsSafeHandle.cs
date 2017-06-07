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
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_channel_credentials from <c>grpc/grpc_security.h</c>
    /// </summary>
    internal class ChannelCredentialsSafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        private ChannelCredentialsSafeHandle()
        {
        }

        public static ChannelCredentialsSafeHandle CreateNullCredentials()
        {
            var creds = new ChannelCredentialsSafeHandle();
            creds.SetHandle(IntPtr.Zero);
            return creds;
        }

        public static ChannelCredentialsSafeHandle CreateSslCredentials(string pemRootCerts, KeyCertificatePair keyCertPair)
        {
            if (keyCertPair != null)
            {
                return Native.grpcsharp_ssl_credentials_create(pemRootCerts, keyCertPair.CertificateChain, keyCertPair.PrivateKey);
            }
            else
            {
                return Native.grpcsharp_ssl_credentials_create(pemRootCerts, null, null);
            }
        }

        public static ChannelCredentialsSafeHandle CreateComposite(ChannelCredentialsSafeHandle channelCreds, CallCredentialsSafeHandle callCreds)
        {
            return Native.grpcsharp_composite_channel_credentials_create(channelCreds, callCreds);
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_channel_credentials_release(handle);
            return true;
        }
    }
}
