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
using System.Runtime.InteropServices;
using System.Text;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Useful methods for native/managed marshalling.
    /// </summary>
    internal static class MarshalUtils
    {
        static readonly Encoding EncodingUTF8 = System.Text.Encoding.UTF8;

        /// <summary>
        /// Converts <c>IntPtr</c> pointing to a UTF-8 encoded byte array to <c>string</c>.
        /// </summary>
        public static unsafe string PtrToStringUTF8(IntPtr ptr, int len)
        {
            if (len == 0)
            {
                return "";
            }

            // allocate a right-sized string and decode into it
            byte* source = (byte*)ptr.ToPointer();
            int charCount = EncodingUTF8.GetCharCount(source, len);
            string s = new string('\0', charCount);
            fixed(char* cPtr = s)
            {
                EncodingUTF8.GetChars(source, len, cPtr, charCount);
            }
            return s;
        }

        /// <summary>
        /// UTF-8 encodes the given string into a buffer of sufficient size
        /// </summary>
        public static unsafe int GetBytesUTF8(string str, byte* destination, int destinationLength)
        {
            int charCount = str.Length;
            if (charCount == 0) return 0;
            fixed (char* source = str)
            {
                return EncodingUTF8.GetBytes(source, charCount, destination, destinationLength);
            }
        }

        /// <summary>
        /// Returns the maximum number of bytes required to encode a given string.
        /// </summary>
        public static int GetMaxBytesUTF8(string str)
        {
            return EncodingUTF8.GetMaxByteCount(str.Length);
        }
    }
}
