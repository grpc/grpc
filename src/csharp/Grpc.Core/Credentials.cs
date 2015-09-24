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
    /// Client-side credentials. Used for creation of a secure channel.
    /// </summary>
    public abstract class Credentials
    {
        static readonly Credentials InsecureInstance = new InsecureCredentialsImpl();

        /// <summary>
        /// Returns instance of credential that provides no security and 
        /// will result in creating an unsecure channel with no encryption whatsoever.
        /// </summary>
        public static Credentials Insecure
        {
            get
            {
                return InsecureInstance;
            }
        }

        /// <summary>
        /// Creates native object for the credentials. May return null if insecure channel
        /// should be created.
        /// </summary>
        /// <returns>The native credentials.</returns>
        internal abstract CredentialsSafeHandle ToNativeCredentials();

        /// <summary>
        /// Returns <c>true</c> if this credential type allows being composed by <c>CompositeCredentials</c>.
        /// </summary>
        internal virtual bool IsComposable
        {
            get { return true; }
        }

        private sealed class InsecureCredentialsImpl : Credentials
        {
            internal override CredentialsSafeHandle ToNativeCredentials()
            {
                return null;
            }

            // Composing insecure credentials makes no sense.
            internal override bool IsComposable
            {
                get { return false; }
            }
        }
    }

    /// <summary>
    /// Client-side SSL credentials.
    /// </summary>
    public sealed class SslCredentials : Credentials
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

        internal override CredentialsSafeHandle ToNativeCredentials()
        {
            return CredentialsSafeHandle.CreateSslCredentials(rootCertificates, keyCertificatePair);
        }
    }

    /// <summary>
    /// Asynchronous authentication interceptor for <see cref="MetadataCredentials"/>.
    /// </summary>
    /// <param name="authUri">URL of a service to which current remote call needs to authenticate</param>
    /// <param name="metadata">Metadata to populate with entries that will be added to outgoing call's headers.</param>
    /// <returns></returns>
    public delegate Task AsyncAuthInterceptor(string authUri, Metadata metadata);

    /// <summary>
    /// Client-side credentials that delegate metadata based auth to an interceptor.
    /// The interceptor is automatically invoked for each remote call that uses <c>MetadataCredentials.</c>
    /// </summary>
    public partial class MetadataCredentials : Credentials
    {
        readonly AsyncAuthInterceptor interceptor;

        /// <summary>
        /// Initializes a new instance of <c>MetadataCredentials</c> class.
        /// </summary>
        /// <param name="interceptor">authentication interceptor</param>
        public MetadataCredentials(AsyncAuthInterceptor interceptor)
        {
            this.interceptor = interceptor;
        }

        internal override CredentialsSafeHandle ToNativeCredentials()
        {
            NativeMetadataCredentialsPlugin plugin = new NativeMetadataCredentialsPlugin(interceptor);
            return plugin.Credentials;
        }
    }

    /// <summary>
    /// Credentials that allow composing multiple credentials objects into one <see cref="Credentials"/> object.
    /// </summary>
    public sealed class CompositeCredentials : Credentials
    {
        readonly List<Credentials> credentials;

        /// <summary>
        /// Initializes a new instance of <c>CompositeCredentials</c> class.
        /// The resulting credentials object will be composite of all the credentials specified as parameters.
        /// </summary>
        /// <param name="credentials">credentials to compose</param>
        public CompositeCredentials(params Credentials[] credentials)
        {
            Preconditions.CheckArgument(credentials.Length >= 2, "Composite credentials object can only be created from 2 or more credentials.");
            foreach (var cred in credentials)
            {
                Preconditions.CheckArgument(cred.IsComposable, "Cannot create composite credentials: one or more credential objects do not allow composition.");
            }
            this.credentials = new List<Credentials>(credentials);
        }

        /// <summary>
        /// Creates a new instance of <c>CompositeCredentials</c> class by composing
        /// multiple <c>Credentials</c> objects.
        /// </summary>
        /// <param name="credentials">credentials to compose</param>
        /// <returns>The new <c>CompositeCredentials</c></returns>
        public static CompositeCredentials Create(params Credentials[] credentials)
        {
            return new CompositeCredentials(credentials);
        }

        internal override CredentialsSafeHandle ToNativeCredentials()
        {
            return ToNativeRecursive(0);
        }

        // Recursive descent makes managing lifetime of intermediate CredentialSafeHandle instances easier.
        // In practice, we won't usually see composites from more than two credentials anyway.
        private CredentialsSafeHandle ToNativeRecursive(int startIndex)
        {
            if (startIndex == credentials.Count - 1)
            {
                return credentials[startIndex].ToNativeCredentials();
            }

            using (var cred1 = credentials[startIndex].ToNativeCredentials())
            using (var cred2 = ToNativeRecursive(startIndex + 1))
            {
                var nativeComposite = CredentialsSafeHandle.CreateComposite(cred1, cred2);
                if (nativeComposite.IsInvalid)
                {
                    throw new ArgumentException("Error creating native composite credentials. Likely, this is because you are trying to compose incompatible credentials.");
                }
                return nativeComposite;
            }
        }
    }
}
