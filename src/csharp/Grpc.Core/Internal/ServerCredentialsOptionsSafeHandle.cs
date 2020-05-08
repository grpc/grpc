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
    /// grpc_ssl_server_credentials_options from <c>grpc/grpc_security.h</c>
    /// </summary>
    internal class ServerCredentialsOptionsSafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        private ServerCredentialsOptionsSafeHandle()
        {
        }

        public static ServerCredentialsOptionsSafeHandle CreateSslServerCredentialsOptionsUsingConfig(SslClientCertificateRequestType sslClientCertificateRequest, ServerCertificateConfigSafeHandle serverCertificateConfig)
        {
            return Native.grpcsharp_ssl_server_credentials_create_options_using_config(sslClientCertificateRequest, serverCertificateConfig);
        }

        public static ServerCredentialsOptionsSafeHandle CreateSslServerCredentialsOptionsUsingConfigFetcher(SslClientCertificateRequestType sslClientCertificateRequest, IntPtr serverCertificateConfigCallbackTag, IntPtr userData)
        {
            return Native.grpcsharp_ssl_server_credentials_create_options_using_config_fetcher(sslClientCertificateRequest, serverCertificateConfigCallbackTag, userData);
        }

        protected override bool ReleaseHandle()
        {
            // Not calling Native.grpcsharp_ssl_server_credentials_options_destroy(handle)
            // here because the user of this object, which is
            // ServerCredentialsSafeHandle CreateSslCredentials()
            // takes the ownership of this object.
            return true;
        }
    }
}
