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
using System.Runtime.InteropServices;
using System.Text;

using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Provides access to read and write metadata values to be exchanged during a call.
    /// </summary>
    public sealed class Metadata : IList<Metadata.Entry>
    {
        /// <summary>
        /// An read-only instance of metadata containing no entries.
        /// </summary>
        public static readonly Metadata Empty = new Metadata().Freeze();

        readonly List<Entry> entries;
        bool readOnly;

        public Metadata()
        {
            this.entries = new List<Entry>();
        }

        public Metadata(ICollection<Entry> entries)
        {
            this.entries = new List<Entry>(entries);
        }

        /// <summary>
        /// Makes this object read-only.
        /// </summary>
        /// <returns>this object</returns>
        public Metadata Freeze()
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
                CheckWriteable();
                entries[index] = value;
            }
        }

        public void Add(Metadata.Entry item)
        {
            CheckWriteable();
            entries.Add(item);
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
            Preconditions.CheckState(!readOnly, "Object is read only");
        }

        #endregion

        /// <summary>
        /// Metadata entry
        /// </summary>
        public struct Entry
        {
            private static readonly Encoding Encoding = Encoding.ASCII;

            readonly string key;
            string value;
            byte[] valueBytes;

            public Entry(string key, byte[] valueBytes)
            {
                this.key = Preconditions.CheckNotNull(key);
                this.value = null;
                this.valueBytes = Preconditions.CheckNotNull(valueBytes);
            }

            public Entry(string key, string value)
            {
                this.key = Preconditions.CheckNotNull(key);
                this.value = Preconditions.CheckNotNull(value);
                this.valueBytes = null;
            }

            public string Key
            {
                get
                {
                    return this.key;
                }
            }

            public byte[] ValueBytes
            {
                get
                {
                    if (valueBytes == null)
                    {
                        valueBytes = Encoding.GetBytes(value);
                    }
                    return valueBytes;
                }
            }

            public string Value
            {
                get
                {
                    if (value == null)
                    {
                        value = Encoding.GetString(valueBytes);
                    }
                    return value;
                }
            }
                
            public override string ToString()
            {
                return string.Format("[Entry: key={0}, value={1}]", Key, Value);
            }
        }
    }
}
