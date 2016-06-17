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
        /// Creates native object for the credentials. May return null if insecure channel
        /// should be created.
        /// </summary>
        /// <returns>The native credentials.</returns>
        internal abstract ChannelCredentialsSafeHandle ToNativeCredentials();

        /// <summary>
        /// Returns <c>true</c> if this credential type allows being composed by <c>CompositeCredentials</c>.
        /// </summary>
        internal virtual bool IsComposable
        {
            get { return false; }
        }

        private sealed class InsecureCredentialsImpl : ChannelCredentials
        {
            internal override ChannelCredentialsSafeHandle ToNativeCredentials()
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

        internal override ChannelCredentialsSafeHandle ToNativeCredentials()
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

        internal override ChannelCredentialsSafeHandle ToNativeCredentials()
        {
            using (var channelCreds = channelCredentials.ToNativeCredentials())
            using (var callCreds = callCredentials.ToNativeCredentials())
            {
                var nativeComposite = ChannelCredentialsSafeHandle.CreateComposite(channelCreds, callCreds);
                if (nativeComposite.IsInvalid)
                {
                    throw new ArgumentException("Error creating native composite credentials. Likely, this is because you are trying to compose incompatible credentials.");
                }
                return nativeComposite;
            }
        }
    }
}
