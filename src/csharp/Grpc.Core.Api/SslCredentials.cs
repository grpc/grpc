#region Copyright notice and license

// Copyright 2019 The gRPC Authors
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

namespace Grpc.Core
{
    /// <summary>
    /// Callback invoked with the expected targetHost and the peer's certificate.
    /// If false is returned by this callback then it is treated as a
    /// verification failure and the attempted connection will fail.
    /// Invocation of the callback is blocking, so any
    /// implementation should be light-weight.
    /// Note that the callback can potentially be invoked multiple times,
    /// concurrently from different threads (e.g. when multiple connections
    /// are being created for the same credentials).
    /// </summary>
    /// <param name="context">The <see cref="T:Grpc.Core.VerifyPeerContext"/> associated with the callback</param>
    /// <returns>true if verification succeeded, false otherwise.</returns>
    /// Note: experimental API that can change or be removed without any prior notice.
    public delegate bool VerifyPeerCallback(VerifyPeerContext context);

    /// <summary>
    /// Client-side SSL credentials.
    /// </summary>
    public sealed class SslCredentials : ChannelCredentials
    {
        readonly string rootCertificates;
        readonly KeyCertificatePair keyCertificatePair;
        readonly VerifyPeerCallback verifyPeerCallback;

        /// <summary>
        /// Creates client-side SSL credentials loaded from
        /// disk file pointed to by the GRPC_DEFAULT_SSL_ROOTS_FILE_PATH environment variable.
        /// If that fails, gets the roots certificates from a well known place on disk.
        /// </summary>
        public SslCredentials() : this(null, null, null)
        {
        }

        /// <summary>
        /// Creates client-side SSL credentials from
        /// a string containing PEM encoded root certificates.
        /// </summary>
        public SslCredentials(string rootCertificates) : this(rootCertificates, null, null)
        {
        }

        /// <summary>
        /// Creates client-side SSL credentials.
        /// </summary>
        /// <param name="rootCertificates">string containing PEM encoded server root certificates.</param>
        /// <param name="keyCertificatePair">a key certificate pair.</param>
        public SslCredentials(string rootCertificates, KeyCertificatePair keyCertificatePair) :
            this(rootCertificates, keyCertificatePair, null)
        {
        }

        /// <summary>
        /// Creates client-side SSL credentials.
        /// </summary>
        /// <param name="rootCertificates">string containing PEM encoded server root certificates.</param>
        /// <param name="keyCertificatePair">a key certificate pair.</param>
        /// <param name="verifyPeerCallback">a callback to verify peer's target name and certificate.</param>
        /// Note: experimental API that can change or be removed without any prior notice.
        public SslCredentials(string rootCertificates, KeyCertificatePair keyCertificatePair, VerifyPeerCallback verifyPeerCallback)
        {
            this.rootCertificates = rootCertificates;
            this.keyCertificatePair = keyCertificatePair;
            this.verifyPeerCallback = verifyPeerCallback;
        }

        /// <summary>
        /// PEM encoding of the server root certificates.
        /// </summary>
        public string RootCertificates
        {
            get
            {
                return this.rootCertificates;
            }
        }

        /// <summary>
        /// Client side key and certificate pair.
        /// If null, client will not use key and certificate pair.
        /// </summary>
        public KeyCertificatePair KeyCertificatePair
        {
            get
            {
                return this.keyCertificatePair;
            }
        }

        /// <summary>
        /// Populates channel credentials configurator with this instance's configuration.
        /// End users never need to invoke this method as it is part of internal implementation.
        /// </summary>
        public override void InternalPopulateConfiguration(ChannelCredentialsConfiguratorBase configurator, object state)
        {
            configurator.SetSslCredentials(state, rootCertificates, keyCertificatePair, verifyPeerCallback);
        }

        internal override bool IsComposable => true;
    }

    
}
