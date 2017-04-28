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
