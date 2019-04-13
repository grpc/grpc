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
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Creates native call credential objects from instances of <c>CallCredentials</c>.
    /// </summary>
    internal class DefaultCallCredentialsConfigurator : CallCredentialsConfiguratorBase
    {
        CallCredentialsSafeHandle nativeCredentials;

        public CallCredentialsSafeHandle NativeCredentials => nativeCredentials;

        public override void SetAsyncAuthInterceptorCredentials(object state, AsyncAuthInterceptor interceptor)
        {
            GrpcPreconditions.CheckState(nativeCredentials == null);

            var plugin = new NativeMetadataCredentialsPlugin(interceptor);
            nativeCredentials = plugin.Credentials;
        }

        public override void SetCompositeCredentials(object state, IReadOnlyList<CallCredentials> credentials)
        {
            GrpcPreconditions.CheckState(nativeCredentials == null);

            GrpcPreconditions.CheckArgument(credentials.Count >= 2);
            nativeCredentials = CompositeToNativeRecursive(credentials, 0);
        }

        // Recursive descent makes managing lifetime of intermediate CredentialSafeHandle instances easier.
        // In practice, we won't usually see composites from more than two credentials anyway.
        private CallCredentialsSafeHandle CompositeToNativeRecursive(IReadOnlyList<CallCredentials> credentials, int startIndex)
        {
            if (startIndex == credentials.Count - 1)
            {
                return credentials[startIndex].ToNativeCredentials();
            }

            using (var cred1 = credentials[startIndex].ToNativeCredentials())
            using (var cred2 = CompositeToNativeRecursive(credentials, startIndex + 1))
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

    internal static class CallCredentialsExtensions
    {
        /// <summary>
        /// Creates native object for the credentials.
        /// </summary>
        /// <returns>The native credentials.</returns>
        public static CallCredentialsSafeHandle ToNativeCredentials(this CallCredentials credentials)
        {
            var configurator = new DefaultCallCredentialsConfigurator();
            credentials.InternalPopulateConfiguration(configurator, credentials);
            return configurator.NativeCredentials;
        }
    }
}
