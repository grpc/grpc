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

using System.Collections.Generic;

namespace Grpc.Core
{
    /// <summary>
    /// Base class for objects that can consume configuration from <c>CallCredentials</c> objects.
    /// Note: experimental API that can change or be removed without any prior notice.
    /// </summary>
    public abstract class ChannelCredentialsConfiguratorBase
    {
        /// <summary>
        /// Configures the credentials to use insecure credentials.
        /// </summary>
        public abstract void SetInsecureCredentials(object state);

        /// <summary>
        /// Configures the credentials to use <c>SslCredentials</c>.
        /// </summary>
        public abstract void SetSslCredentials(object state, string rootCertificates, KeyCertificatePair keyCertificatePair, VerifyPeerCallback verifyPeerCallback);

        /// <summary>
        /// Configures the credentials to use composite channel credentials (a composite of channel credentials and call credentials).
        /// </summary>
        public abstract void SetCompositeCredentials(object state, ChannelCredentials channelCredentials, CallCredentials callCredentials);
    }
}
