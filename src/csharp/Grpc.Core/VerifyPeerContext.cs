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
    /// Verification context for VerifyPeerCallback.
    /// Note: experimental API that can change or be removed without any prior notice.
    /// </summary>
    public class VerifyPeerContext
    {
        /// <summary>
        /// Initializes a new instance of the <see cref="T:Grpc.Core.VerifyPeerContext"/> class.
        /// </summary>
        /// <param name="targetHost">string containing the host name of the peer.</param>
        /// <param name="targetPem">string containing PEM encoded certificate of the peer.</param>
        internal VerifyPeerContext(string targetHost, string targetPem)
        {
            this.TargetHost = targetHost;
            this.TargetPem = targetPem;
        }

        /// <summary>
        /// String containing the host name of the peer.
        /// </summary>
        public string TargetHost { get; }

        /// <summary>
        /// string containing PEM encoded certificate of the peer.
        /// </summary>
        public string TargetPem { get; }
    }
}
