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
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Client-side channel credentials. Used for creation of a secure channel.
    /// </summary>
    public abstract class ChannelCredentials
    {
        static readonly ChannelCredentials InsecureInstance = new InsecureCredentialsImpl();
        readonly Lazy<ChannelCredentialsSafeHandle> cachedNativeCredentials;

        /// <summary>
        /// Creates a new instance of channel credentials
        /// </summary>
        public ChannelCredentials()
        {
            // Native credentials object need to be kept alive once initialized for subchannel sharing to work correctly
            // with secure connections. See https://github.com/grpc/grpc/issues/15207.
            // We rely on finalizer to clean up the native portion of ChannelCredentialsSafeHandle after the ChannelCredentials
            // instance becomes unused.
            this.cachedNativeCredentials = new Lazy<ChannelCredentialsSafeHandle>(() => CreateNativeCredentials());
        }

        /// <summary>
        /// Returns instance of credentials that provides no security and 
        /// will result in creating an unsecure channel with no encryption whatsoever.
        /// </summary>
        public static ChannelCredentials Insecure
        {
            get
            {
                return InsecureInstance;
            }
        }

        /// <summary>
        /// Creates a new instance of <c>ChannelCredentials</c> class by composing
        /// given channel credentials with call credentials.
        /// </summary>
        /// <param name="channelCredentials">Channel credentials.</param>
        /// <param name="callCredentials">Call credentials.</param>
        /// <returns>The new composite <c>ChannelCredentials</c></returns>
        public static ChannelCredentials Create(ChannelCredentials channelCredentials, CallCredentials callCredentials)
        {
            return new CompositeChannelCredentials(channelCredentials, callCredentials);
        }

        /// <summary>
        /// Gets native object for the credentials, creating one if it already doesn't exist. May return null if insecure channel
        /// should be created. Caller must not call <c>Dispose()</c> on the returned native credentials as their lifetime
        /// is managed by this class (and instances of native credentials are cached).
        /// </summary>
        /// <returns>The native credentials.</returns>
        internal ChannelCredentialsSafeHandle GetNativeCredentials()
        {
            return cachedNativeCredentials.Value;
        }

        /// <summary>
        /// Creates a new native object for the credentials. May return null if insecure channel
        /// should be created. For internal use only, use <see cref="GetNativeCredentials"/> instead.
        /// </summary>
        /// <returns>The native credentials.</returns>
        internal abstract ChannelCredentialsSafeHandle CreateNativeCredentials();

        /// <summary>
        /// Returns <c>true</c> if this credential type allows being composed by <c>CompositeCredentials</c>.
        /// </summary>
        internal virtual bool IsComposable
        {
            get { return false; }
        }

        private sealed class InsecureCredentialsImpl : ChannelCredentials
        {
            internal override ChannelCredentialsSafeHandle CreateNativeCredentials()
            {
                return null;
            }
        }
    }

    /// <summary>
    /// Client-side SSL credentials.
    /// </summary>
    public sealed class SslCredentials : ChannelCredentials
    {
        readonly string rootCertificates;
        readonly KeyCertificatePair keyCertificatePair;

        /// <summary>
        /// Creates client-side SSL credentials loaded from
        /// disk file pointed to by the GRPC_DEFAULT_SSL_ROOTS_FILE_PATH environment variable.
        /// If that fails, gets the roots certificates from a well known place on disk.
        /// </summary>
        public SslCredentials() : this(null, null)
        {
        }

        /// <summary>
        /// Creates client-side SSL credentials from
        /// a string containing PEM encoded root certificates.
        /// </summary>
        public SslCredentials(string rootCertificates) : this(rootCertificates, null)
        {
        }
            
        /// <summary>
        /// Creates client-side SSL credentials.
        /// </summary>
        /// <param name="rootCertificates">string containing PEM encoded server root certificates.</param>
        /// <param name="keyCertificatePair">a key certificate pair.</param>
        public SslCredentials(string rootCertificates, KeyCertificatePair keyCertificatePair)
        {
            this.rootCertificates = rootCertificates;
            this.keyCertificatePair = keyCertificatePair;
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

        // Composing composite makes no sense.
        internal override bool IsComposable
        {
            get { return true; }
        }

        internal override ChannelCredentialsSafeHandle CreateNativeCredentials()
        {
            return ChannelCredentialsSafeHandle.CreateSslCredentials(rootCertificates, keyCertificatePair);
        }
    }

    /// <summary>
    /// Credentials that allow composing one <see cref="ChannelCredentials"/> object and 
    /// one or more <see cref="CallCredentials"/> objects into a single <see cref="ChannelCredentials"/>.
    /// </summary>
    internal sealed class CompositeChannelCredentials : ChannelCredentials
    {
        readonly ChannelCredentials channelCredentials;
        readonly CallCredentials callCredentials;

        /// <summary>
        /// Initializes a new instance of <c>CompositeChannelCredentials</c> class.
        /// The resulting credentials object will be composite of all the credentials specified as parameters.
        /// </summary>
        /// <param name="channelCredentials">channelCredentials to compose</param>
        /// <param name="callCredentials">channelCredentials to compose</param>
        public CompositeChannelCredentials(ChannelCredentials channelCredentials, CallCredentials callCredentials)
        {
            this.channelCredentials = GrpcPreconditions.CheckNotNull(channelCredentials);
            this.callCredentials = GrpcPreconditions.CheckNotNull(callCredentials);
            GrpcPreconditions.CheckArgument(channelCredentials.IsComposable, "Supplied channel credentials do not allow composition.");
        }

        internal override ChannelCredentialsSafeHandle CreateNativeCredentials()
        {
            using (var callCreds = callCredentials.ToNativeCredentials())
            {
                var nativeComposite = ChannelCredentialsSafeHandle.CreateComposite(channelCredentials.GetNativeCredentials(), callCreds);
                if (nativeComposite.IsInvalid)
                {
                    throw new ArgumentException("Error creating native composite credentials. Likely, this is because you are trying to compose incompatible credentials.");
                }
                return nativeComposite;
            }
        }
    }
}
