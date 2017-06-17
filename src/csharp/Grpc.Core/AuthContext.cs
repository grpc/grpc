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
using System.Linq;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Authentication context for a call.
    /// AuthContext is the only reliable source of truth when it comes to authenticating calls.
    /// Using any other call/context properties for authentication purposes is wrong and inherently unsafe.
    /// Note: experimental API that can change or be removed without any prior notice.
    /// </summary>
    public class AuthContext
    {
        string peerIdentityPropertyName;
        Dictionary<string, List<AuthProperty>> properties;

        /// <summary>
        /// Initializes a new instance of the <see cref="T:Grpc.Core.AuthContext"/> class.
        /// </summary>
        /// <param name="peerIdentityPropertyName">Peer identity property name.</param>
        /// <param name="properties">Multimap of auth properties by name.</param>
        internal AuthContext(string peerIdentityPropertyName, Dictionary<string, List<AuthProperty>> properties)
        {
            this.peerIdentityPropertyName = peerIdentityPropertyName;
            this.properties = GrpcPreconditions.CheckNotNull(properties);
        }

        /// <summary>
        /// Returns <c>true</c> if the peer is authenticated.
        /// </summary>
        public bool IsPeerAuthenticated
        {
            get
            {
                return peerIdentityPropertyName != null;
            }
        }

        /// <summary>
        /// Gets the name of the property that indicates the peer identity. Returns <c>null</c>
        /// if the peer is not authenticated.
        /// </summary>
        public string PeerIdentityPropertyName
        {
            get
            {
                return peerIdentityPropertyName;
            }
        }

        /// <summary>
        /// Gets properties that represent the peer identity (there can be more than one). Returns an empty collection
        /// if the peer is not authenticated.
        /// </summary>
        public IEnumerable<AuthProperty> PeerIdentity
        {
            get
            {
                if (peerIdentityPropertyName == null)
                {
                    return Enumerable.Empty<AuthProperty>();
                }
                return properties[peerIdentityPropertyName];
            }
        }

        /// <summary>
        /// Gets the auth properties of this context.
        /// </summary>
        public IEnumerable<AuthProperty> Properties
        {
            get
            {
                return properties.Values.SelectMany(v => v);
            }
        }

        /// <summary>
        /// Returns the auth properties with given name (there can be more than one).
        /// If no properties of given name exist, an empty collection will be returned.
        /// </summary>
        public IEnumerable<AuthProperty> FindPropertiesByName(string propertyName)
        {
            List<AuthProperty> result;
            if (!properties.TryGetValue(propertyName, out result))
            {
                return Enumerable.Empty<AuthProperty>();
            }
            return result;
        }
    }
}
