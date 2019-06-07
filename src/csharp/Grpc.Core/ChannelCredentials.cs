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
using System.Runtime.InteropServices;
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Logging;
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
}
