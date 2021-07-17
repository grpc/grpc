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

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using Grpc.Core.Utils;
using Grpc.Core.Logging;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Creates native call credential objects from instances of <c>ChannelCredentials</c>.
    /// </summary>
    internal class DefaultChannelCredentialsConfigurator : ChannelCredentialsConfiguratorBase
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<DefaultCallCredentialsConfigurator>();

        // Native credentials object need to be kept alive once initialized for subchannel sharing to work correctly
        // with secure connections. See https://github.com/grpc/grpc/issues/15207.
        // We rely on finalizer to clean up the native portion of ChannelCredentialsSafeHandle after the ChannelCredentials
        // instance becomes unused.
        static readonly ConditionalWeakTable<ChannelCredentials, Lazy<ChannelCredentialsSafeHandle>> CachedNativeCredentials = new ConditionalWeakTable<ChannelCredentials, Lazy<ChannelCredentialsSafeHandle>>();
        static readonly object StaticLock = new object();

        bool configured;
        ChannelCredentialsSafeHandle nativeCredentials;

        public ChannelCredentialsSafeHandle NativeCredentials => nativeCredentials;
        
        public override void SetInsecureCredentials(object state)
        {
            GrpcPreconditions.CheckState(!configured);
            // null corresponds to insecure credentials.
            configured = true;
            nativeCredentials = null;
        }

        public override void SetSslCredentials(object state, string rootCertificates, KeyCertificatePair keyCertificatePair, VerifyPeerCallback verifyPeerCallback)
        {
            GrpcPreconditions.CheckState(!configured);
            configured = true;
            nativeCredentials = GetOrCreateNativeCredentials((ChannelCredentials) state,
                () => CreateNativeSslCredentials(rootCertificates, keyCertificatePair, verifyPeerCallback));
        }

        public override void SetCompositeCredentials(object state, ChannelCredentials channelCredentials, CallCredentials callCredentials)
        {
            GrpcPreconditions.CheckState(!configured);
            configured = true;
            nativeCredentials = GetOrCreateNativeCredentials((ChannelCredentials) state,
                () => CreateNativeCompositeCredentials(channelCredentials, callCredentials));
        }

        private ChannelCredentialsSafeHandle CreateNativeSslCredentials(string rootCertificates, KeyCertificatePair keyCertificatePair, VerifyPeerCallback verifyPeerCallback)
        {
            IntPtr verifyPeerCallbackTag = IntPtr.Zero;
            if (verifyPeerCallback != null)
            {
                verifyPeerCallbackTag = new VerifyPeerCallbackRegistration(verifyPeerCallback).CallbackRegistration.Tag;
            }
            return ChannelCredentialsSafeHandle.CreateSslCredentials(rootCertificates, keyCertificatePair, verifyPeerCallbackTag);
        }

        private ChannelCredentialsSafeHandle CreateNativeCompositeCredentials(ChannelCredentials channelCredentials, CallCredentials callCredentials)
        {
            using (var callCreds = callCredentials.ToNativeCredentials())
            {
                var nativeComposite = ChannelCredentialsSafeHandle.CreateComposite(channelCredentials.ToNativeCredentials(), callCreds);
                if (nativeComposite.IsInvalid)
                {
                    throw new ArgumentException("Error creating native composite credentials. Likely, this is because you are trying to compose incompatible credentials.");
                }
                return nativeComposite;
            }
        }

        private ChannelCredentialsSafeHandle GetOrCreateNativeCredentials(ChannelCredentials key, Func<ChannelCredentialsSafeHandle> nativeCredentialsFactory)
        {
            Lazy<ChannelCredentialsSafeHandle> lazyValue;
            lock (StaticLock) {
                if (!CachedNativeCredentials.TryGetValue(key, out lazyValue))
                {
                    lazyValue = new Lazy<ChannelCredentialsSafeHandle>(nativeCredentialsFactory);
                    CachedNativeCredentials.Add(key, lazyValue);
                }
            }
            return lazyValue.Value;
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

    internal static class ChannelCredentialsExtensions
    {
        /// <summary>
        /// Creates native object for the credentials.
        /// </summary>
        /// <returns>The native credentials.</returns>
        public static ChannelCredentialsSafeHandle ToNativeCredentials(this ChannelCredentials credentials)
        {
            var configurator = new DefaultChannelCredentialsConfigurator();
            credentials.InternalPopulateConfiguration(configurator, credentials);
            return configurator.NativeCredentials;
        }
    }
}
