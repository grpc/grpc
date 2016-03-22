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

        [Test]
        public void IndexOf()
        {
            var metadata = CreateMetadata();
            Assert.AreEqual(0, metadata.IndexOf(metadata[0]));
            Assert.AreEqual(1, metadata.IndexOf(metadata[1]));
        }

        [Test]
        public void Insert()
        {
            var metadata = CreateMetadata();
            metadata.Insert(0, new Metadata.Entry("new-key", "new-value"));
            Assert.AreEqual(3, metadata.Count);
            Assert.AreEqual("new-key", metadata[0].Key);
            Assert.AreEqual("abc", metadata[1].Key);
        }

        [Test]
        public void RemoveAt()
        {
            var metadata = CreateMetadata();
            metadata.RemoveAt(0);
            Assert.AreEqual(1, metadata.Count);
            Assert.AreEqual("xyz", metadata[0].Key);
        }

        [Test]
        public void Remove()
        {
            var metadata = CreateMetadata();
            metadata.Remove(metadata[0]);
            Assert.AreEqual(1, metadata.Count);
            Assert.AreEqual("xyz", metadata[0].Key);
        }

        [Test]
        public void Indexer_Set()
        {
            var metadata = CreateMetadata();
            var entry = new Metadata.Entry("new-key", "new-value");

            metadata[1] = entry;
            Assert.AreEqual(entry, metadata[1]);
        }

        [Test]
        public void Clear()
        {
            var metadata = CreateMetadata();
            metadata.Clear();
            Assert.AreEqual(0, metadata.Count);
        }

        [Test]
        public void Contains()
        {
            var metadata = CreateMetadata();
            Assert.IsTrue(metadata.Contains(metadata[0]));
            Assert.IsFalse(metadata.Contains(new Metadata.Entry("new-key", "new-value")));
        }

        [Test]
        public void CopyTo()
        {
            var metadata = CreateMetadata();
            var array = new Metadata.Entry[metadata.Count + 1];

            metadata.CopyTo(array, 1);
            Assert.AreEqual(default(Metadata.Entry), array[0]);
            Assert.AreEqual(metadata[0], array[1]);
        }

        [Test]
        public void IEnumerableGetEnumerator()
        {
            var metadata = CreateMetadata();
            var enumerator = (metadata as System.Collections.IEnumerable).GetEnumerator();
            
            int i = 0;
            while (enumerator.MoveNext())
            {
                Assert.AreEqual(metadata[i], enumerator.Current);
                i++;
            }
        }

        [Test]
        public void FreezeMakesReadOnly()
        {
            var entry = new Metadata.Entry("new-key", "new-value");
            var metadata = CreateMetadata().Freeze();

            Assert.IsTrue(metadata.IsReadOnly);
            Assert.Throws<InvalidOperationException>(() => metadata.Insert(0, entry));
            Assert.Throws<InvalidOperationException>(() => metadata.RemoveAt(0));
            Assert.Throws<InvalidOperationException>(() => metadata[0] = entry);
            Assert.Throws<InvalidOperationException>(() => metadata.Add(entry));
            Assert.Throws<InvalidOperationException>(() => metadata.Add("new-key", "new-value"));
            Assert.Throws<InvalidOperationException>(() => metadata.Add("new-key-bin", new byte[] { 0xaa }));
            Assert.Throws<InvalidOperationException>(() => metadata.Clear());
            Assert.Throws<InvalidOperationException>(() => metadata.Remove(metadata[0]));
        }

        private Metadata CreateMetadata()
        {
            return new Metadata
            {
                { "abc", "abc-value" },
                { "xyz", "xyz-value" },
            };
        }
    }
}
