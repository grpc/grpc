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
            new Metadata.Entry("abc.xyz", "XYZ");
            new Metadata.Entry("abc.xyz-bin", new byte[] {1, 2, 3});
            Assert.Throws(typeof(ArgumentException), () => new Metadata.Entry("abc[", "xyz"));
            Assert.Throws(typeof(ArgumentException), () => new Metadata.Entry("abc/", "xyz"));
        }

        [Test]
        public void KeysAreNormalized_UppercaseKey()
        {
            var uppercaseKey = "ABC";
            var entry = new Metadata.Entry(uppercaseKey, "XYZ");
            Assert.AreEqual("abc", entry.Key);
        }

        [Test]
        public void KeysAreNormalized_LowercaseKey()
        {
            var lowercaseKey = "abc";
            var entry = new Metadata.Entry(lowercaseKey, "XYZ");
            // no allocation if key already lowercase
            Assert.AreSame(lowercaseKey, entry.Key);
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
        public void IndexOf([Values(0, 1, 2, 3)] int metadataCount)
        {
            var metadata = CreateMetadata(metadataCount);
            Assert.AreEqual(-1, metadata.IndexOf(new Metadata.Entry("new-key", "new-value")));
            if (metadataCount > 0) Assert.AreEqual(0, metadata.IndexOf(metadata[0]));
            if (metadataCount > 1) Assert.AreEqual(1, metadata.IndexOf(metadata[1]));
        }

        [Test]
        public void Insert([Values(0, 1, 2, 3)] int metadataCount)
        {
            var metadata = CreateMetadata(metadataCount);
            metadata.Insert(0, new Metadata.Entry("new-key", "new-value"));
            Assert.AreEqual(metadataCount + 1, metadata.Count);
            Assert.AreEqual("new-key", metadata[0].Key);
            if (metadataCount != 0) Assert.AreEqual("abc", metadata[1].Key);
        }

        [Test]
        public void RemoveAt([Values(0, 1, 2, 3)] int metadataCount)
        {
            var metadata = CreateMetadata(metadataCount);

            if (metadataCount == 0)
            {
                Assert.Throws<ArgumentOutOfRangeException>(delegate { metadata.RemoveAt(0); });
            }
            else
            {
                metadata.RemoveAt(0);
                Assert.AreEqual(metadataCount - 1, metadata.Count);
                if (metadataCount != 1) Assert.AreEqual("xyz", metadata[0].Key);
            }
        }

        [Test]
        public void Remove([Values(0, 1, 2, 3)] int metadataCount)
        {
            var metadata = CreateMetadata(metadataCount);

            Assert.IsFalse(metadata.Remove(new Metadata.Entry("zzz", "zzzfdask")));
            if (metadataCount != 0)
            {
                Assert.IsTrue(metadata.Remove(metadata[0]));
                Assert.AreEqual(metadataCount - 1, metadata.Count);
                if (metadataCount != 1) Assert.AreEqual("xyz", metadata[0].Key);
            }
        }

        [Test]
        public void Indexer_Set([Values(0, 1, 2, 3)] int metadataCount)
        {
            var metadata = CreateMetadata(metadataCount);
            var entry = new Metadata.Entry("new-key", "new-value");

            int index = metadataCount == 0 ? 0 : metadataCount - 1;

            metadata[index] = entry;
            Assert.AreEqual(entry, metadata[index]);
        }

        [Test]
        public void Clear([Values(0, 1, 2, 3)] int metadataCount)
        {
            var metadata = CreateMetadata(metadataCount);
            metadata.Clear();
            Assert.AreEqual(0, metadata.Count);
        }

        [Test]
        public void Contains([Values(0, 1, 2, 3)] int metadataCount)
        {
            var metadata = CreateMetadata(metadataCount);
            if (metadataCount != 0) Assert.IsTrue(metadata.Contains(metadata[0]));
            Assert.IsFalse(metadata.Contains(new Metadata.Entry("new-key", "new-value")));
        }

        [Test]
        public void CopyTo([Values(0, 1, 2, 3)] int metadataCount)
        {
            var metadata = CreateMetadata(metadataCount);
            var array = new Metadata.Entry[metadata.Count + 1];

            metadata.CopyTo(array, 1);
            Assert.AreEqual(default(Metadata.Entry), array[0]);
            if (metadataCount != 0) Assert.AreEqual(metadata[0], array[1]);
        }

        [Test]
        public void IEnumerableGetEnumerator([Values(0, 1, 2, 3)] int metadataCount)
        {
            var metadata = CreateMetadata(metadataCount);
            var enumerator = (metadata as System.Collections.IEnumerable).GetEnumerator();
            
            int i = 0;
            while (enumerator.MoveNext())
            {
                Assert.AreEqual(metadata[i], enumerator.Current);
                i++;
            }
            Assert.AreEqual(metadataCount, i);
        }

        [Test]
        public void ForEeach([Values(0, 1, 2, 3)] int metadataCount)
        {
            var metadata = CreateMetadata(metadataCount);

            int i = 0;
            foreach(var value in metadata)
            {
                Assert.AreEqual(metadata[i], value);
                i++;
            }
            Assert.AreEqual(metadataCount, i);
        }

        [Test]
        public void FreezeMakesReadOnly([Values(0, 1, 2, 3)] int metadataCount)
        {
            var entry = new Metadata.Entry("new-key", "new-value");
            var metadata = CreateMetadata(metadataCount).Freeze();

            Assert.IsTrue(metadata.IsReadOnly);
            Assert.Throws<InvalidOperationException>(() => metadata.Insert(0, entry));
            Assert.Throws<InvalidOperationException>(() => metadata.RemoveAt(0));
            Assert.Throws<InvalidOperationException>(() => metadata[0] = entry);
            Assert.Throws<InvalidOperationException>(() => metadata.Add(entry));
            Assert.Throws<InvalidOperationException>(() => metadata.Add("new-key", "new-value"));
            Assert.Throws<InvalidOperationException>(() => metadata.Add("new-key-bin", new byte[] { 0xaa }));
            Assert.Throws<InvalidOperationException>(() => metadata.Clear());
            Assert.Throws<InvalidOperationException>(() => metadata.Remove(new Metadata.Entry("zzz", "asdasd")));
        }

        private Metadata CreateMetadata(int count)
        {
            var meta = new Metadata();
            if (count > 0) meta.Add("abc", "abc-value");
            if (count > 1) meta.Add("xyz", "xyz-value");
            if (count > 2) meta.Add("def", "def-value");
            return meta;
        }
    }
}
