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
    /// Client-side call credentials. Provide authorization with per-call granularity.
    /// </summary>
    public abstract class CallCredentials
    {
        /// <summary>
        /// Composes multiple multiple <c>CallCredentials</c> objects into
        /// a single <c>CallCredentials</c> object.
        /// </summary>
        /// <param name="credentials">credentials to compose</param>
        /// <returns>The new <c>CompositeCallCredentials</c></returns>
        public static CallCredentials Compose(params CallCredentials[] credentials)
        {
            return new CompositeCallCredentials(credentials);
        }

        /// <summary>
        /// Creates a new instance of <c>CallCredentials</c> class from an
        /// interceptor that can attach metadata to outgoing calls.
        /// </summary>
        /// <param name="interceptor">authentication interceptor</param>
        public static CallCredentials FromInterceptor(AsyncAuthInterceptor interceptor)
        {
            return new MetadataCredentials(interceptor);
        }

        /// <summary>
        /// Creates native object for the credentials.
        /// </summary>
        /// <returns>The native credentials.</returns>
        internal abstract CallCredentialsSafeHandle ToNativeCredentials();
    }

    /// <summary>
    /// Client-side credentials that delegate metadata based auth to an interceptor.
    /// The interceptor is automatically invoked for each remote call that uses <c>MetadataCredentials.</c>
    /// </summary>
    internal sealed class MetadataCredentials : CallCredentials
    {
        readonly AsyncAuthInterceptor interceptor;

        /// <summary>
        /// Initializes a new instance of <c>MetadataCredentials</c> class.
        /// </summary>
        /// <param name="interceptor">authentication interceptor</param>
        public MetadataCredentials(AsyncAuthInterceptor interceptor)
        {
            this.interceptor = Preconditions.CheckNotNull(interceptor);
        }

        internal override CallCredentialsSafeHandle ToNativeCredentials()
        {
            NativeMetadataCredentialsPlugin plugin = new NativeMetadataCredentialsPlugin(interceptor);
            return plugin.Credentials;
        }
    }

    /// <summary>
    /// Credentials that allow composing multiple credentials objects into one <see cref="CallCredentials"/> object.
    /// </summary>
    internal sealed class CompositeCallCredentials : CallCredentials
    {
        readonly List<CallCredentials> credentials;

        /// <summary>
        /// Initializes a new instance of <c>CompositeCallCredentials</c> class.
        /// The resulting credentials object will be composite of all the credentials specified as parameters.
        /// </summary>
        /// <param name="credentials">credentials to compose</param>
        public CompositeCallCredentials(params CallCredentials[] credentials)
        {
            Preconditions.CheckArgument(credentials.Length >= 2, "Composite credentials object can only be created from 2 or more credentials.");
            this.credentials = new List<CallCredentials>(credentials);
        }

        internal override CallCredentialsSafeHandle ToNativeCredentials()
        {
            return ToNativeRecursive(0);
        }

        // Recursive descent makes managing lifetime of intermediate CredentialSafeHandle instances easier.
        // In practice, we won't usually see composites from more than two credentials anyway.
        private CallCredentialsSafeHandle ToNativeRecursive(int startIndex)
        {
            if (startIndex == credentials.Count - 1)
            {
                return credentials[startIndex].ToNativeCredentials();
            }

            using (var cred1 = credentials[startIndex].ToNativeCredentials())
            using (var cred2 = ToNativeRecursive(startIndex + 1))
            {
                var nativeComposite = CallCredentialsSafeHandle.CreateComposite(cred1, cred2);
                if (nativeComposite.IsInvalid)
                {
                    throw new ArgumentException("Error creating native composite credentials. Likely, this is because you are trying to compose incompatible credentials.");
                }
                return nativeComposite;
            }
        }
    }
}
