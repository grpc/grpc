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
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<SslCredentials>();

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

        // Composing composite makes no sense.
        internal override bool IsComposable
        {
            get { return true; }
        }

        internal override ChannelCredentialsSafeHandle CreateNativeCredentials()
        {
            IntPtr verifyPeerCallbackTag = IntPtr.Zero;
            if (verifyPeerCallback != null)
            {
                verifyPeerCallbackTag = new VerifyPeerCallbackRegistration(verifyPeerCallback).CallbackRegistration.Tag;
            }
            return ChannelCredentialsSafeHandle.CreateSslCredentials(rootCertificates, keyCertificatePair, verifyPeerCallbackTag);
        }

        private class VerifyPeerCallbackRegistration
        {
            readonly VerifyPeerCallback verifyPeerCallback;
            readonly NativeCallbackRegistration callbackRegistration;

            public VerifyPeerCallbackRegistration(VerifyPeerCallback verifyPeerCallback)
            {
                this.verifyPeerCallback = verifyPeerCallback;
                this.callbackRegistration = NativeCallbackDispatcher.RegisterCallback(HandleUniversalCallback);
            }

            public NativeCallbackRegistration CallbackRegistration => callbackRegistration;

            private int HandleUniversalCallback(IntPtr arg0, IntPtr arg1, IntPtr arg2, IntPtr arg3, IntPtr arg4, IntPtr arg5)
            {
                return VerifyPeerCallbackHandler(arg0, arg1, arg2 != IntPtr.Zero);
            }

            private int VerifyPeerCallbackHandler(IntPtr targetName, IntPtr peerPem, bool isDestroy)
            {
                if (isDestroy)
                {
                    this.callbackRegistration.Dispose();
                    return 0;
                }

                try
                {
                    var context = new VerifyPeerContext(Marshal.PtrToStringAnsi(targetName), Marshal.PtrToStringAnsi(peerPem));

                    return this.verifyPeerCallback(context) ? 0 : 1;
                }
                catch (Exception e)
                {
                    // eat the exception, we must not throw when inside callback from native code.
                    Logger.Error(e, "Exception occurred while invoking verify peer callback handler.");
                    // Return validation failure in case of exception.
                    return 1;
                }
            }
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
