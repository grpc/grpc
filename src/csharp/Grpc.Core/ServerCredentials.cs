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
    /// Modes of requesting client's SSL certificate by the server.
    /// Corresponds to <c>grpc_ssl_client_certificate_request_type</c>.
    /// </summary>
    public enum SslClientCertificateRequestType
    {
        /// <summary>
        /// Server does not request client certificate.
        /// The certificate presented by the client is not checked by the server at
        /// all. (A client may present a self signed or signed certificate or not
        /// present a certificate at all and any of those option would be accepted)
        /// </summary>
        DontRequest = 0,
        /// <summary>
        /// Server requests client certificate but does not enforce that the client
        /// presents a certificate.
        /// If the client presents a certificate, the client authentication is left to
        /// the application (the necessary metadata will be available to the
        /// application via authentication context properties, see grpc_auth_context).
        /// The client's key certificate pair must be valid for the SSL connection to
        /// be established.
        ///</summary>
        RequestButDontVerify,
        /// <summary>
        /// Server requests client certificate but does not enforce that the client
        /// presents a certificate.
        /// If the client presents a certificate, the client authentication is done by
        /// the gRPC framework. (For a successful connection the client needs to either
        /// present a certificate that can be verified against the root certificate
        /// configured by the server or not present a certificate at all)
        /// The client's key certificate pair must be valid for the SSL connection to
        /// be established.
        /// </summary>
        RequestAndVerify,
        /// <summary>
        /// Server requests client certificate and enforces that the client presents a
        /// certificate.
        /// If the client presents a certificate, the client authentication is left to
        /// the application (the necessary metadata will be available to the
        /// application via authentication context properties, see grpc_auth_context).
        /// The client's key certificate pair must be valid for the SSL connection to
        /// be established.
        ///</summary>
        RequestAndRequireButDontVerify,
        /// <summary>
        /// Server requests client certificate and enforces that the client presents a
        /// certificate.
        /// The certificate presented by the client is verified by the gRPC framework.
        /// (For a successful connection the client needs to present a certificate that
        /// can be verified against the root certificate configured by the server)
        /// The client's key certificate pair must be valid for the SSL connection to
        /// be established.
        /// </summary>
        RequestAndRequireAndVerify,
    }
    
    /// <summary>
    /// Callback results for dynamically loading a SSL certificate config.
    /// Corresponds to <c>grpc_ssl_certificate_config_reload_status</c>.
    /// </summary>
    public enum SslCertificateConfigReloadStatus
    {
        /// <summary>
        /// Corresponds to <c>GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED</c>.
        /// </summary>
        Unchanged = 0,

        /// <summary>
        /// Corresponds to <c>GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW</c>.
        /// </summary>
        New,

        /// <summary>
        /// Corresponds to <c>GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL</c>.
        /// </summary>
        Fail,
    }


    /// <summary>
    /// Callback to retrieve updated SSL server certificates, private keys, and
    /// trusted CAs(for client authentication).
    /// </summary>
    /// <returns>Server certificate configuration to use.</returns>
    public delegate ServerCertificateConfig ServerCertificateConfigCallback();

    /// <summary>
    /// Server-side SSL credentials.
    /// </summary>
    public class SslServerCredentials : ServerCredentials
    {
        readonly IList<KeyCertificatePair> keyCertificatePairs;
        readonly string rootCertificates;
        readonly SslClientCertificateRequestType clientCertificateRequest;
        readonly ServerCertificateConfigCallback serverCertificateConfigCallback;

        /// <summary>
        /// Creates server-side SSL credentials.
        /// </summary>
        /// <param name="keyCertificatePairs">Key-certificates to use.</param>
        /// <param name="rootCertificates">PEM encoded client root certificates used to authenticate client.</param>
        /// <param name="forceClientAuth">Deprecated, use clientCertificateRequest overload instead.</param>
        public SslServerCredentials(IEnumerable<KeyCertificatePair> keyCertificatePairs, string rootCertificates, bool forceClientAuth)
            : this(keyCertificatePairs, rootCertificates, forceClientAuth ? SslClientCertificateRequestType.RequestAndRequireAndVerify : SslClientCertificateRequestType.DontRequest)
        {
        }

        /// <summary>
        /// Creates server-side SSL credentials.
        /// </summary>
        /// <param name="keyCertificatePairs">Key-certificates to use.</param>
        /// <param name="rootCertificates">PEM encoded client root certificates used to authenticate client.</param>
        /// <param name="clientCertificateRequest">Options for requesting and verifying client certificate.</param>
        public SslServerCredentials(IEnumerable<KeyCertificatePair> keyCertificatePairs, string rootCertificates, SslClientCertificateRequestType clientCertificateRequest)
            : this(keyCertificatePairs, rootCertificates, clientCertificateRequest, null)
        {
        }

        /// <summary>
        /// Creates server-side SSL credentials.
        /// This constructor should be used if you wish to reload the certificates
        /// and keys of the SSL server without interrupting the operation of the server.
        /// Initial certificate config will be fetched during server initialization.
        /// </summary>
        /// <param name="serverCertificateConfig">Server certificate configuration to use.</param>
        /// <param name="clientCertificateRequest">Options for requesting and verifying client certificate.</param>
        /// <param name="serverCertificateConfigCallback">Callback for certificate config fetcher.</param>
        public SslServerCredentials(ServerCertificateConfig serverCertificateConfig, SslClientCertificateRequestType clientCertificateRequest, ServerCertificateConfigCallback serverCertificateConfigCallback)
            : this(
                  serverCertificateConfig.KeyCertificatePairs,
                  serverCertificateConfig.RootCertificates,
                  clientCertificateRequest,
                  serverCertificateConfigCallback)
        {
        }

        /// <summary>
        /// Creates server-side SSL credentials.
        /// This constructor should be used if you wish to reload the certificates
        /// and keys of the SSL server without interrupting the operation of the server.
        /// Initial certificate config will be fetched during server initialization.
        /// </summary>
        /// <param name="keyCertificatePairs">Key-certificates to use.</param>
        /// <param name="rootCertificates">PEM encoded client root certificates used to authenticate client.</param>
        /// <param name="clientCertificateRequest">Options for requesting and verifying client certificate.</param>
        /// <param name="serverCertificateConfigCallback">Callback for certificate config fetcher.</param>
        public SslServerCredentials(IEnumerable<KeyCertificatePair> keyCertificatePairs, string rootCertificates, SslClientCertificateRequestType clientCertificateRequest, ServerCertificateConfigCallback serverCertificateConfigCallback)
        {
            this.keyCertificatePairs = new List<KeyCertificatePair>(keyCertificatePairs).AsReadOnly();
            GrpcPreconditions.CheckArgument(this.keyCertificatePairs.Count > 0,
                "At least one KeyCertificatePair needs to be provided.");
            if (clientCertificateRequest == SslClientCertificateRequestType.RequestAndRequireAndVerify)
            {
                GrpcPreconditions.CheckNotNull(rootCertificates,
                    "Cannot require and verify client certificate unless you provide rootCertificates.");
            }
            this.rootCertificates = rootCertificates;
            this.clientCertificateRequest = clientCertificateRequest;
            this.serverCertificateConfigCallback = serverCertificateConfigCallback;
        }

        /// <summary>
        /// Creates server-side SSL credentials.
        /// This constructor should be used if you do not wish to authenticate the client.
        /// (client certificate won't be requested and checked by the server at all).
        /// </summary>
        /// <param name="keyCertificatePairs">Key-certificates to use.</param>
        public SslServerCredentials(IEnumerable<KeyCertificatePair> keyCertificatePairs) : this(keyCertificatePairs, null, SslClientCertificateRequestType.DontRequest)
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
        /// Deprecated. If true, the authenticity of client check will be enforced.
        /// </summary>
        public bool ForceClientAuthentication
        {
            get
            {
                return this.clientCertificateRequest == SslClientCertificateRequestType.RequestAndRequireAndVerify;
            }
        }

        /// <summary>
        /// Mode of requesting certificate from client by the server.
        /// </summary>
        public SslClientCertificateRequestType ClientCertificateRequest
        {
            get
            {
                return this.clientCertificateRequest;
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

            IntPtr serverCertificateConfigCallbackTag = IntPtr.Zero;
            if (serverCertificateConfigCallback != null)
            {
                serverCertificateConfigCallbackTag = new ServerCertificateConfigCallbackRegistration(serverCertificateConfigCallback).CallbackRegistration.Tag;
            }

            return ServerCredentialsSafeHandle.CreateSslCredentials(
                rootCertificates,
                certChains,
                keys,
                clientCertificateRequest,
                serverCertificateConfigCallbackTag,
                userData: IntPtr.Zero);
        }
    }
}
