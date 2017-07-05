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
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class AuthPropertyTest
    {
        [Test]
        public void Create_NameIsNotNull()
        {
            Assert.Throws(typeof(ArgumentNullException), () => AuthProperty.Create(null, new byte[0]));
            Assert.Throws(typeof(ArgumentNullException), () => AuthProperty.CreateUnsafe(null, new byte[0]));
        }

        [Test]
        public void Create_ValueIsNotNull()
        {
            Assert.Throws(typeof(ArgumentNullException), () => AuthProperty.Create("abc", null));
            Assert.Throws(typeof(ArgumentNullException), () => AuthProperty.CreateUnsafe("abc", null));
        }

        [Test]
        public void Create()
        {
            var valueBytes = new byte[] { 68, 69, 70 };
            var authProperty = AuthProperty.Create("abc", valueBytes);

            Assert.AreEqual("abc", authProperty.Name);
            Assert.AreNotSame(valueBytes, authProperty.ValueBytesUnsafe);
            CollectionAssert.AreEqual(valueBytes, authProperty.ValueBytes);
            CollectionAssert.AreEqual(valueBytes, authProperty.ValueBytesUnsafe);
            Assert.AreEqual("DEF", authProperty.Value);
        }

        [Test]
        public void CreateUnsafe()
        {
            var valueBytes = new byte[] { 68, 69, 70 };
            var authProperty = AuthProperty.CreateUnsafe("abc", valueBytes);

            Assert.AreEqual("abc", authProperty.Name);
            Assert.AreSame(valueBytes, authProperty.ValueBytesUnsafe);
            Assert.AreNotSame(valueBytes, authProperty.ValueBytes);
            CollectionAssert.AreEqual(valueBytes, authProperty.ValueBytes);
            CollectionAssert.AreEqual(valueBytes, authProperty.ValueBytesUnsafe);
            Assert.AreEqual("DEF", authProperty.Value);
        }
    }
}
