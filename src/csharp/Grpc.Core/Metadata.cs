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
using System.Text;
using System.Text.RegularExpressions;

using Grpc.Core.Internal;
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

        // TODO: add support for access by key

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
        public void Add(string key, string value)
        {
            Add(new Entry(key, value));
        }

        /// <summary>
        /// <see cref="T:IList`1"/>
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
            private static readonly Regex ValidKeyRegex = new Regex("^[a-z0-9_-]+$");

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
            /// <param name="key">Metadata key, needs to have suffix indicating a binary valued metadata entry.</param>
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
            /// Initializes a new instance of the <see cref="Grpc.Core.Metadata.Entry"/> struct holding an ASCII value.
            /// </summary>
            /// <param name="key">Metadata key, must not use suffix indicating a binary valued metadata entry.</param>
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
                        return MarshalUtils.GetBytesASCII(value);
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
                    return value ?? MarshalUtils.GetStringASCII(valueBytes);
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
                return valueBytes ?? MarshalUtils.GetBytesASCII(value);
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
                return new Entry(key, MarshalUtils.GetStringASCII(valueBytes), null);
            }

            private static string NormalizeKey(string key)
            {
                var normalized = GrpcPreconditions.CheckNotNull(key, "key").ToLowerInvariant();
                GrpcPreconditions.CheckArgument(ValidKeyRegex.IsMatch(normalized), 
                    "Metadata entry key not valid. Keys can only contain lowercase alphanumeric characters, underscores and hyphens.");
                return normalized;
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
