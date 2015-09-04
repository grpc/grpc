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
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class MetadataTest
    {
        [Test]
        public void AsciiEntry()
        {
            var entry = new Metadata.Entry("ABC", "XYZ");
            Assert.IsFalse(entry.IsBinary);
            Assert.AreEqual("abc", entry.Key);  // key is in lowercase.
            Assert.AreEqual("XYZ", entry.Value);
            CollectionAssert.AreEqual(new[] { (byte)'X', (byte)'Y', (byte)'Z' }, entry.ValueBytes);

            Assert.Throws(typeof(ArgumentException), () => new Metadata.Entry("abc-bin", "xyz"));

            Assert.AreEqual("[Entry: key=abc, value=XYZ]", entry.ToString());
        }

        [Test]
        public void BinaryEntry()
        {
            var bytes = new byte[] { 1, 2, 3 };
            var entry = new Metadata.Entry("ABC-BIN", bytes);
            Assert.IsTrue(entry.IsBinary);
            Assert.AreEqual("abc-bin", entry.Key);  // key is in lowercase.
            Assert.Throws(typeof(InvalidOperationException), () => { var v = entry.Value; });
            CollectionAssert.AreEqual(bytes, entry.ValueBytes);

            Assert.Throws(typeof(ArgumentException), () => new Metadata.Entry("abc", bytes));

            Assert.AreEqual("[Entry: key=abc-bin, valueBytes=System.Byte[]]", entry.ToString());
        }

        [Test]
        public void AsciiEntry_KeyValidity()
        {
            new Metadata.Entry("ABC", "XYZ");
            new Metadata.Entry("0123456789abc", "XYZ");
            new Metadata.Entry("-abc", "XYZ");
            new Metadata.Entry("a_bc_", "XYZ");
            Assert.Throws(typeof(ArgumentException), () => new Metadata.Entry("abc[", "xyz"));
            Assert.Throws(typeof(ArgumentException), () => new Metadata.Entry("abc/", "xyz"));
        }

        [Test]
        public void Entry_ConstructionPreconditions()
        {
            Assert.Throws(typeof(ArgumentNullException), () => new Metadata.Entry(null, "xyz"));
            Assert.Throws(typeof(ArgumentNullException), () => new Metadata.Entry("abc", (string)null));
            Assert.Throws(typeof(ArgumentNullException), () => new Metadata.Entry("abc-bin", (byte[])null));
        }

        [Test]
        public void Entry_Immutable()
        {
            var origBytes = new byte[] { 1, 2, 3 };
            var bytes = new byte[] { 1, 2, 3 };
            var entry = new Metadata.Entry("ABC-BIN", bytes);
            bytes[0] = 255;  // changing the array passed to constructor should have any effect.
            CollectionAssert.AreEqual(origBytes, entry.ValueBytes);

            entry.ValueBytes[0] = 255;
            CollectionAssert.AreEqual(origBytes, entry.ValueBytes);
        }

        [Test]
        public void Entry_CreateUnsafe_Ascii()
        {
            var bytes = new byte[] { (byte)'X', (byte)'y' };
            var entry = Metadata.Entry.CreateUnsafe("abc", bytes);
            Assert.IsFalse(entry.IsBinary);
            Assert.AreEqual("abc", entry.Key);
            Assert.AreEqual("Xy", entry.Value);
            CollectionAssert.AreEqual(bytes, entry.ValueBytes);
        }

        [Test]
        public void Entry_CreateUnsafe_Binary()
        {
            var bytes = new byte[] { 1, 2, 3 };
            var entry = Metadata.Entry.CreateUnsafe("abc-bin", bytes);
            Assert.IsTrue(entry.IsBinary);
            Assert.AreEqual("abc-bin", entry.Key);
            Assert.Throws(typeof(InvalidOperationException), () => { var v = entry.Value; });
            CollectionAssert.AreEqual(bytes, entry.ValueBytes);
        }
    }
}
