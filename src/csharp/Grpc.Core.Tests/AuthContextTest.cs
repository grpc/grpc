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
using NUnit.Framework;
using Grpc.Core;
using System.Linq;

namespace Grpc.Core.Tests
{
    public class AuthContextTest
    {
        [Test]
        public void EmptyContext()
        {
            var context = new AuthContext(null, new Dictionary<string, List<AuthProperty>>());
            Assert.IsFalse(context.IsPeerAuthenticated);
            Assert.IsNull(context.PeerIdentityPropertyName);
            Assert.AreEqual(0, context.PeerIdentity.Count());
            Assert.AreEqual(0, context.Properties.Count());
            Assert.AreEqual(0, context.FindPropertiesByName("nonexistent").Count());
        }

        [Test]
        public void AuthenticatedContext()
        {
            var property1 = AuthProperty.Create("abc", new byte[] { 68, 69, 70 });
            var context = new AuthContext("some_identity", new Dictionary<string, List<AuthProperty>>
            {
                {"some_identity", new List<AuthProperty> {property1}}
            });
            Assert.IsTrue(context.IsPeerAuthenticated);
            Assert.AreEqual("some_identity", context.PeerIdentityPropertyName);
            Assert.AreEqual(1, context.PeerIdentity.Count());
        }

        [Test]
        public void FindPropertiesByName()
        {
            var property1 = AuthProperty.Create("abc", new byte[] {68, 69, 70});
            var property2 = AuthProperty.Create("abc", new byte[] {71, 72, 73 });
            var property3 = AuthProperty.Create("abc", new byte[] {});
            var context = new AuthContext(null, new Dictionary<string, List<AuthProperty>>
            {
                {"existent", new List<AuthProperty> {property1, property2}},
                {"foobar", new List<AuthProperty> {property3}},
            });
            Assert.AreEqual(3, context.Properties.Count());
            Assert.AreEqual(0, context.FindPropertiesByName("nonexistent").Count());

            var existentProperties = new List<AuthProperty>(context.FindPropertiesByName("existent"));
            Assert.AreEqual(2, existentProperties.Count);
        }
    }
}
