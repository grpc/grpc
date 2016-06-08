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
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;

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

        public int IndexOf(Metadata.Entry item)
        {
            return entries.IndexOf(item);
        }

        public void Insert(int index, Metadata.Entry item)
        {
            GrpcPreconditions.CheckNotNull(item);
            CheckWriteable();
            entries.Insert(index, item);
        }

        public void RemoveAt(int index)
        {
            CheckWriteable();
            entries.RemoveAt(index);
        }

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

        public void Add(Metadata.Entry item)
        {
            GrpcPreconditions.CheckNotNull(item);
            CheckWriteable();
            entries.Add(item);
        }

        public void Add(string key, string value)
        {
            Add(new Entry(key, value));
        }

        public void Add(string key, byte[] valueBytes)
        {
            Add(new Entry(key, valueBytes));
        }

        public void Clear()
        {
            CheckWriteable();
            entries.Clear();
        }

        public bool Contains(Metadata.Entry item)
        {
            return entries.Contains(item);
        }

        public void CopyTo(Metadata.Entry[] array, int arrayIndex)
        {
            entries.CopyTo(array, arrayIndex);
        }

        public int Count
        {
            get { return entries.Count; }
        }

        public bool IsReadOnly
        {
            get { return readOnly; }
        }

        public bool Remove(Metadata.Entry item)
        {
            CheckWriteable();
            return entries.Remove(item);
        }

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
            private static readonly Encoding Encoding = Encoding.ASCII;
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
                GrpcPreconditions.CheckArgument(this.key.EndsWith(BinaryHeaderSuffix),
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
                GrpcPreconditions.CheckArgument(!this.key.EndsWith(BinaryHeaderSuffix),
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
                        return Encoding.GetBytes(value);
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
                    return value ?? Encoding.GetString(valueBytes);
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
                return valueBytes ?? Encoding.GetBytes(value);
            }

            /// <summary>
            /// Creates a binary value or ascii value metadata entry from data received from the native layer.
            /// We trust C core to give us well-formed data, so we don't perform any checks or defensive copying.
            /// </summary>
            internal static Entry CreateUnsafe(string key, byte[] valueBytes)
            {
                if (key.EndsWith(BinaryHeaderSuffix))
                {
                    return new Entry(key, null, valueBytes);
                }
                return new Entry(key, Encoding.GetString(valueBytes), null);
            }

            private static string NormalizeKey(string key)
            {
                var normalized = GrpcPreconditions.CheckNotNull(key, "key").ToLowerInvariant();
                GrpcPreconditions.CheckArgument(ValidKeyRegex.IsMatch(normalized), 
                    "Metadata entry key not valid. Keys can only contain lowercase alphanumeric characters, underscores and hyphens.");
                return normalized;
            }
        }
    }
}
