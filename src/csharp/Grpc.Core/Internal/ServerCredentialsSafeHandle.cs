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
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_server_credentials from <c>grpc/grpc_security.h</c>
    /// </summary>
    internal class ServerCredentialsSafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        private ServerCredentialsSafeHandle()
        {
        }

        public static ServerCredentialsSafeHandle CreateSslCredentials(string pemRootCerts, string[] keyCertPairCertChainArray, string[] keyCertPairPrivateKeyArray, SslClientCertificateRequestType clientCertificateRequest)
        {
            GrpcPreconditions.CheckArgument(keyCertPairCertChainArray.Length == keyCertPairPrivateKeyArray.Length);
            return Native.grpcsharp_ssl_server_credentials_create(pemRootCerts,
                                                                  keyCertPairCertChainArray, keyCertPairPrivateKeyArray,
                                                                  new UIntPtr((ulong)keyCertPairCertChainArray.Length),
                                                                  clientCertificateRequest);
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_server_credentials_release(handle);
            return true;
        }
    }
}
