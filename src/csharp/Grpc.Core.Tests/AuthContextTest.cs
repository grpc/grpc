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
