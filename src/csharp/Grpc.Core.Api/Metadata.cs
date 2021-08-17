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
using System.Runtime.InteropServices;
using System.Text;
using Grpc.Core.Api.Utils;

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

        readonly List<Entry> entries;
        bool readOnly;

        /// <summary>
        /// Initializes a new instance of <c>Metadata</c>.
        /// </summary>
        public Metadata()
        {
            this.entries = new List<Entry>();
        }

        /// <summary>
        /// Makes this object read-only.
        /// </summary>
        /// <returns>this object</returns>
        internal Metadata Freeze()
        {
            this.readOnly = true;
            return this;
        }

        /// <summary>
        /// Gets the last metadata entry with the specified key.
        /// If there are no matching entries then <c>null</c> is returned.
        /// </summary>
        public Entry Get(string key)
        {
            for (int i = entries.Count - 1; i >= 0; i--)
            {
                if (entries[i].Key == key)
                {
                    return entries[i];
                }
            }

            return null;
        }

        /// <summary>
        /// Gets the string value of the last metadata entry with the specified key.
        /// If the metadata entry is binary then an exception is thrown.
        /// If there are no matching entries then <c>null</c> is returned.
        /// </summary>
        public string GetValue(string key)
        {
            return Get(key)?.Value;
        }

        /// <summary>
        /// Gets the bytes value of the last metadata entry with the specified key.
        /// If the metadata entry is not binary the string value will be returned as ASCII encoded bytes.
        /// If there are no matching entries then <c>null</c> is returned.
        /// </summary>
        public byte[] GetValueBytes(string key)
        {
            return Get(key)?.ValueBytes;
        }

        /// <summary>
        /// Gets all metadata entries with the specified key.
        /// </summary>
        public IEnumerable<Entry> GetAll(string key)
        {
            for (int i = 0; i < entries.Count; i++)
            {
                if (entries[i].Key == key)
                {
                    yield return entries[i];
                }
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

        #region IList members


        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public int IndexOf(Metadata.Entry item)
        {
            return entries.IndexOf(item);
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public void Insert(int index, Metadata.Entry item)
        {
            GrpcPreconditions.CheckNotNull(item);
            CheckWriteable();
            entries.Insert(index, item);
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public void RemoveAt(int index)
        {
            CheckWriteable();
            entries.RemoveAt(index);
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public Metadata.Entry this[int index]
        {
            get
            {
                return entries[index];
            }

            set
            {
                GrpcPreconditions.CheckNotNull(value);
                CheckWriteable();
                entries[index] = value;
            }
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public void Add(Metadata.Entry item)
        {
            GrpcPreconditions.CheckNotNull(item);
            CheckWriteable();
            entries.Add(item);
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public void Clear()
        {
            CheckWriteable();
            entries.Clear();
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public bool Contains(Metadata.Entry item)
        {
            return entries.Contains(item);
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public void CopyTo(Metadata.Entry[] array, int arrayIndex)
        {
            entries.CopyTo(array, arrayIndex);
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public int Count
        {
            get { return entries.Count; }
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
            return entries.Remove(item);
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
        /// </summary>
        public IEnumerator<Metadata.Entry> GetEnumerator()
        {
            return entries.GetEnumerator();
        }

        IEnumerator System.Collections.IEnumerable.GetEnumerator()
        {
            return entries.GetEnumerator();
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
            /// If the metadata entry is not binary the string value will be returned as ASCII encoded bytes.
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
            /// If the metadata entry is binary then an exception is thrown.
            /// </summary>
            public string Value
            {
                get
                {
                    GrpcPreconditions.CheckState(!IsBinary, "Cannot access string value of a binary metadata entry");
                    return value;
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
            internal static Entry CreateUnsafe(string key, IntPtr source, int length)
            {
                if (HasBinaryHeaderSuffix(key))
                {
                    byte[] arr;
                    if (length == 0)
                    {
                        arr = EmptyByteArray;
                    }
                    else
                    {   // create a local copy in a fresh array
                        arr = new byte[length];
                        Marshal.Copy(source, arr, 0, length);
                    }
                    return new Entry(key, null, arr);
                }
                else
                {
                    string s = EncodingASCII.GetString(source, length);
                    return new Entry(key, s, null);
                }
            }

            static readonly byte[] EmptyByteArray = new byte[0];

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
                        c == '-' )
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
