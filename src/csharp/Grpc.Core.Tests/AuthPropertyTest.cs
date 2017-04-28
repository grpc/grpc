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
