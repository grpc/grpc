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
using System.Collections.Generic;
using System.Text;

using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Dictionary of string instances that can be looked up by their binary
    /// representation (ASCII encoded).
    /// </summary>
    internal class BinaryStringDictionary
    {
        static readonly Encoding EncodingACII = System.Text.Encoding.ASCII;
        Dictionary<BinaryKey, string> dict = new Dictionary<BinaryKey, string>();

        public BinaryStringDictionary()
        {
        }

        public void Add(string str)
        {
            dict.Add(new BinaryKey(EncodingACII.GetBytes(str)), str);
        }

        /// <summary>
        /// Looks up the string based on its ASCII-encoded binary form.
        /// </summary>
        public string Lookup(IntPtr nativeData, int nativeDataLen)
        {
            var key = new BinaryKey(nativeData, nativeDataLen);
            dict.TryGetValue(key, out string str);
            return str;
        }
        
        private struct BinaryKey : IEquatable<BinaryKey>
        {
            private byte[] data;
            private IntPtr nativeData;
            private int nativeDataLen;

            public BinaryKey(byte[] data)
            {
                this.data = GrpcPreconditions.CheckNotNull(data);
                this.nativeData = IntPtr.Zero;
                this.nativeDataLen = 0;
            }

            public BinaryKey(IntPtr nativeData, int nativeDataLen)
            {
                GrpcPreconditions.CheckArgument(nativeData != IntPtr.Zero);
                GrpcPreconditions.CheckArgument(nativeDataLen >= 0);
                this.data = null;
                this.nativeData = nativeData;
                this.nativeDataLen = nativeDataLen;
            }

            /// <summary>
            /// Indicates whether this instance and a specified object are equal.
            /// </summary>
            public override bool Equals(object obj)
            {
                return obj is BinaryKey && Equals((BinaryKey)obj);
            }

            /// <summary>
            /// Returns the hash code for this instance.
            /// </summary>
            public override int GetHashCode()
            {
                return ComputeHashCode(GetSpan());
            }

            /// <summary>
            /// Indicates whether this instance and a specified object are equal.
            /// </summary>
            public bool Equals(BinaryKey other)
            {
                return GetSpan().SequenceEqual(other.GetSpan());
            }

            private ReadOnlySpan<byte> GetSpan()
            {
                if (this.data != null)
                {
                    return new ReadOnlySpan<byte>(this.data);
                }
                if (this.nativeData != IntPtr.Zero)
                {
                    unsafe { return new ReadOnlySpan<byte>((byte*)this.nativeData, this.nativeDataLen); }
                }
                throw new ArgumentNullException("Content cannot be null.");
            }

            private static int ComputeHashCode(ReadOnlySpan<byte> data)
            {
                unchecked
                {
                    // TODO(jtattermusch): there should be a faster way of computing some meaninful hashcode
                    // for a block of memory, but for now this should be sufficient.
                    int hc = 1768953197;
                    for (int i=0; i < data.Length; i++)
                    {
                       hc = hc * 17 + data[i];
                    }
                    return hc;
                }
            }
        }
    }
}
