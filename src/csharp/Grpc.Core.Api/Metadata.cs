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
using System.Collections;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Text;

using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// A collection of metadata entries that can be exchanged during a call.
    /// gRPC supports these types of metadata:
    /// <list type="bullet">
    /// <item><term>Request headers</term><description>are sent by the client at the beginning of a remote call before any request messages are sent.</description></item>
    /// <item><term>Response headers</term><description>are sent by the server at the beginning of a remote call handler before any response messages are sent.</description></item>
    /// <item><term>Response trailers</term><description>are sent by the server at the end of a remote call along with resulting call status.</description></item>
    /// </list>
    /// </summary>
    public sealed class Metadata : IList<Metadata.Entry>
    {
        /// <summary>
        /// All binary headers should have this suffix.
        /// </summary>
        public const string BinaryHeaderSuffix = "-bin";

        /// <summary>
        /// An read-only instance of metadata containing no entries.
        /// </summary>
        public static readonly Metadata Empty = new Metadata().Freeze();

        /// <summary>
        /// To be used in initial metadata to request specific compression algorithm
        /// for given call. Direct selection of compression algorithms is an internal
        /// feature and is not part of public API.
        /// </summary>
        internal const string CompressionRequestAlgorithmMetadataKey = "grpc-internal-encoding-request";
        static readonly Encoding EncodingASCII = System.Text.Encoding.ASCII;

        private object entryOrEntries; // zero items: null; one item: the Entry itself; more than one: List<Entry>

        bool readOnly;

        /// <summary>
        /// Initializes a new instance of <c>Metadata</c>.
        /// </summary>
        public Metadata() { }

        /// <summary>
        /// Makes this object read-only.
        /// </summary>
        /// <returns>this object</returns>
        internal Metadata Freeze()
        {
            this.readOnly = true;
            return this;
        }

        // TODO: add support for access by key

        #region IList members


        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public int IndexOf(Metadata.Entry item)
        {
            if (item == null | entryOrEntries == null) return -1;
            if (entryOrEntries is Entry) return item.Equals(entryOrEntries) ? 0 : -1;
            return ((List<Entry>)entryOrEntries).IndexOf(item);
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public void Insert(int index, Metadata.Entry item)
        {
            GrpcPreconditions.CheckNotNull(item);
            CheckWriteable();
            if (entryOrEntries == null)
            {
                if (index != 0) ThrowArgumentOutOfRange();
                entryOrEntries = item;
            }
            else
            {
                var list = entryOrEntries as List<Entry>;
                if (list == null)
                {
                    switch (index)
                    {   // ensure a valid position *before* we allocate a list
                        case 0:
                        case 1:
                            break; // fine
                        default:
                            ThrowArgumentOutOfRange();
                            break;
                    }
                    list = new List<Entry>();
                    list.Add((Entry)entryOrEntries);
                    entryOrEntries = list;
                }
                list.Insert(index, item);
            }
        }

        [MethodImpl(MethodImplOptions.NoInlining)]
        private static void ThrowArgumentOutOfRange()
        {
            throw new ArgumentOutOfRangeException();
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public void RemoveAt(int index)
        {
            CheckWriteable();
            if (entryOrEntries == null)
            {
                ThrowArgumentOutOfRange();
            }

            if (entryOrEntries is Entry)
            {
                if (index == 0) entryOrEntries = null;
                else ThrowArgumentOutOfRange();
            }
            else
            {
                ((List<Entry>)entryOrEntries).RemoveAt(index);
            }
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public Metadata.Entry this[int index]
        {
            get
            {
                if (entryOrEntries == null)
                {
                    ThrowArgumentOutOfRange();
                }
                else if (entryOrEntries is Entry)
                {
                    if (index != 0) ThrowArgumentOutOfRange();
                    return (Entry)entryOrEntries;
                }
                return ((List<Entry>)entryOrEntries)[index];
            }

            set
            {
                GrpcPreconditions.CheckNotNull(value);
                CheckWriteable();

                if (entryOrEntries == null)
                {
                    if (index != 0) ThrowArgumentOutOfRange();
                    entryOrEntries = value;
                }
                else if (entryOrEntries is Entry)
                {
                    switch(index)
                    {
                        case 0: // overwrite
                            entryOrEntries = value;
                            break;
                        case 1: // append
                            var list = new List<Entry>();
                            list.Add((Entry)entryOrEntries);
                            list.Add(value);
                            entryOrEntries = list;
                            break;
                        default:
                            ThrowArgumentOutOfRange();
                            break;
                    }
                }
                else
                {
                    ((List<Entry>)entryOrEntries)[index] = value;
                }
            }
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public void Add(Metadata.Entry item)
        {
            GrpcPreconditions.CheckNotNull(item);
            CheckWriteable();

            if (entryOrEntries == null)
            {
                entryOrEntries = item;
            }
            else
            {
                var list = entryOrEntries as List<Entry>;
                if (list == null)
                {
                    list = new List<Entry>();
                    list.Add((Entry)entryOrEntries);
                    entryOrEntries = list;
                }
                list.Add(item);
            }
        }

        /// <summary>
        /// Adds a new ASCII-valued metadata entry. See <c>Metadata.Entry</c> constructor for params.
        /// </summary>
        public void Add(string key, string value)
        {
            Add(new Entry(key, value));
        }

        /// <summary>
        /// Adds a new binary-valued metadata entry. See <c>Metadata.Entry</c> constructor for params.
        /// </summary>
        public void Add(string key, byte[] valueBytes)
        {
            Add(new Entry(key, valueBytes));
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public void Clear()
        {
            CheckWriteable();

            var list = entryOrEntries as List<Entry>;
            if (list == null)
            {
                entryOrEntries = null; // simple wipe
            }
            else
            {
                list.Clear();
            }
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public bool Contains(Metadata.Entry item)
        {
            if (item == null | entryOrEntries == null) return false;
            if (entryOrEntries is Entry) return item.Equals(entryOrEntries);
            return ((List<Entry>)entryOrEntries).Contains(item);
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public void CopyTo(Metadata.Entry[] array, int arrayIndex)
        {
            if (entryOrEntries == null) { }
            else
            {
                var list = entryOrEntries as List<Entry>;
                if (list == null)
                {
                    array[arrayIndex] = (Entry)entryOrEntries;
                }
                else
                {
                    list.CopyTo(array, arrayIndex);
                }
            }
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public int Count
        {
            get
            {
                if (entryOrEntries == null) return 0;
                var list = entryOrEntries as List<Entry>;
                return list == null ? 1 : list.Count;
            }
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public bool IsReadOnly
        {
            get { return readOnly; }
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public bool Remove(Metadata.Entry item)
        {
            CheckWriteable();
            if (item == null | entryOrEntries == null) return false;
            if (entryOrEntries is Entry)
            {
                if (item.Equals(entryOrEntries))
                {
                    entryOrEntries = null;
                    return true;
                }
                return false;
            }
            return ((List<Entry>)entryOrEntries).Remove(item);
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public IEnumerator<Entry> GetEnumerator()
        {
            if (entryOrEntries == null) return EmptyEnumerator<Entry>.Instance;
            var list = entryOrEntries as List<Entry>;
            if (list != null) return list.GetEnumerator();
            return new SingleEnumerator<Entry>((Entry)entryOrEntries);
        }

        IEnumerator IEnumerable.GetEnumerator()
        {
            return GetEnumerator();
        }

        private sealed class EmptyEnumerator<T> : IEnumerator<T>
        {
            private EmptyEnumerator() { }
            public static readonly EmptyEnumerator<T> Instance = new EmptyEnumerator<T>();

            T IEnumerator<T>.Current { get { return default(T); } }
            object IEnumerator.Current { get { return null; } }
            void IDisposable.Dispose() { }
            bool IEnumerator.MoveNext() { return false; }
            void IEnumerator.Reset() { }
        }
        private sealed class SingleEnumerator<T> : IEnumerator<T>
        {
            private readonly T value;
            private bool eof;
            public SingleEnumerator(T value)
            {
                this.value = value;
            }
            T IEnumerator<T>.Current { get { return value; } }
            object IEnumerator.Current { get { return value; } }
            void IDisposable.Dispose() { }
            bool IEnumerator.MoveNext()
            {
                if (eof) return false;
                eof = true;
                return true;
            }

            void IEnumerator.Reset() { eof = false; }
        }

        private void CheckWriteable()
        {
            GrpcPreconditions.CheckState(!readOnly, "Object is read only");
        }

        #endregion

        /// <summary>
        /// Metadata entry
        /// </summary>
        public class Entry
        {
            readonly string key;
            readonly string value;
            readonly byte[] valueBytes;

            private Entry(string key, string value, byte[] valueBytes)
            {
                this.key = key;
                this.value = value;
                this.valueBytes = valueBytes;
            }

            /// <summary>
            /// Initializes a new instance of the <see cref="Grpc.Core.Metadata.Entry"/> struct with a binary value.
            /// </summary>
            /// <param name="key">Metadata key. Gets converted to lowercase. Needs to have suffix indicating a binary valued metadata entry. Can only contain lowercase alphanumeric characters, underscores, hyphens and dots.</param>
            /// <param name="valueBytes">Value bytes.</param>
            public Entry(string key, byte[] valueBytes)
            {
                this.key = NormalizeKey(key);
                GrpcPreconditions.CheckArgument(HasBinaryHeaderSuffix(this.key),
                    "Key for binary valued metadata entry needs to have suffix indicating binary value.");
                this.value = null;
                GrpcPreconditions.CheckNotNull(valueBytes, "valueBytes");
                this.valueBytes = new byte[valueBytes.Length];
                Buffer.BlockCopy(valueBytes, 0, this.valueBytes, 0, valueBytes.Length);  // defensive copy to guarantee immutability
            }

            /// <summary>
            /// Initializes a new instance of the <see cref="Grpc.Core.Metadata.Entry"/> struct with an ASCII value.
            /// </summary>
            /// <param name="key">Metadata key. Gets converted to lowercase. Must not use suffix indicating a binary valued metadata entry. Can only contain lowercase alphanumeric characters, underscores, hyphens and dots.</param>
            /// <param name="value">Value string. Only ASCII characters are allowed.</param>
            public Entry(string key, string value)
            {
                this.key = NormalizeKey(key);
                GrpcPreconditions.CheckArgument(!HasBinaryHeaderSuffix(this.key),
                    "Key for ASCII valued metadata entry cannot have suffix indicating binary value.");
                this.value = GrpcPreconditions.CheckNotNull(value, "value");
                this.valueBytes = null;
            }

            /// <summary>
            /// Gets the metadata entry key.
            /// </summary>
            public string Key
            {
                get
                {
                    return this.key;
                }
            }

            /// <summary>
            /// Gets the binary value of this metadata entry.
            /// </summary>
            public byte[] ValueBytes
            {
                get
                {
                    if (valueBytes == null)
                    {
                        return EncodingASCII.GetBytes(value);
                    }

                    // defensive copy to guarantee immutability
                    var bytes = new byte[valueBytes.Length];
                    Buffer.BlockCopy(valueBytes, 0, bytes, 0, valueBytes.Length);
                    return bytes;
                }
            }

            /// <summary>
            /// Gets the string value of this metadata entry.
            /// </summary>
            public string Value
            {
                get
                {
                    GrpcPreconditions.CheckState(!IsBinary, "Cannot access string value of a binary metadata entry");
                    return value ?? EncodingASCII.GetString(valueBytes);
                }
            }

            /// <summary>
            /// Returns <c>true</c> if this entry is a binary-value entry.
            /// </summary>
            public bool IsBinary
            {
                get
                {
                    return value == null;
                }
            }

            /// <summary>
            /// Returns a <see cref="System.String"/> that represents the current <see cref="Grpc.Core.Metadata.Entry"/>.
            /// </summary>
            public override string ToString()
            {
                if (IsBinary)
                {
                    return string.Format("[Entry: key={0}, valueBytes={1}]", key, valueBytes);
                }

                return string.Format("[Entry: key={0}, value={1}]", key, value);
            }

            /// <summary>
            /// Gets the serialized value for this entry. For binary metadata entries, this leaks
            /// the internal <c>valueBytes</c> byte array and caller must not change contents of it.
            /// </summary>
            internal byte[] GetSerializedValueUnsafe()
            {
                return valueBytes ?? EncodingASCII.GetBytes(value);
            }

            /// <summary>
            /// Creates a binary value or ascii value metadata entry from data received from the native layer.
            /// We trust C core to give us well-formed data, so we don't perform any checks or defensive copying.
            /// </summary>
            internal static Entry CreateUnsafe(string key, byte[] valueBytes)
            {
                if (HasBinaryHeaderSuffix(key))
                {
                    return new Entry(key, null, valueBytes);
                }
                return new Entry(key, EncodingASCII.GetString(valueBytes), null);
            }

            private static string NormalizeKey(string key)
            {
                GrpcPreconditions.CheckNotNull(key, "key");

                GrpcPreconditions.CheckArgument(IsValidKey(key, out bool isLowercase),
                    "Metadata entry key not valid. Keys can only contain lowercase alphanumeric characters, underscores, hyphens and dots.");
                if (isLowercase)
                {
                    // save allocation of a new string if already lowercase
                    return key;
                }

                return key.ToLowerInvariant();
            }

            private static bool IsValidKey(string input, out bool isLowercase)
            {
                isLowercase = true;
                for (int i = 0; i < input.Length; i++)
                {
                    char c = input[i];
                    if ('a' <= c && c <= 'z' ||
                        '0' <= c && c <= '9' ||
                        c == '.' ||
                        c == '_' ||
                        c == '-')
                        continue;

                    if ('A' <= c && c <= 'Z')
                    {
                        isLowercase = false;
                        continue;
                    }

                    return false;
                }

                return true;
            }

            /// <summary>
            /// Returns <c>true</c> if the key has "-bin" binary header suffix.
            /// </summary>
            private static bool HasBinaryHeaderSuffix(string key)
            {
                // We don't use just string.EndsWith because its implementation is extremely slow
                // on CoreCLR and we've seen significant differences in gRPC benchmarks caused by it.
                // See https://github.com/dotnet/coreclr/issues/5612

                int len = key.Length;
                if (len >= 4 &&
                    key[len - 4] == '-' &&
                    key[len - 3] == 'b' &&
                    key[len - 2] == 'i' &&
                    key[len - 1] == 'n')
                {
                    return true;
                }
                return false;
            }
        }
    }
}
