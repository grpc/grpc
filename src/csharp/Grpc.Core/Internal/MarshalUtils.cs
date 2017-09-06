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
        static readonly Encoding EncodingASCII = System.Text.Encoding.ASCII;

        /// <summary>
        /// Converts <c>IntPtr</c> pointing to a UTF-8 encoded byte array to <c>string</c>.
        /// </summary>
        public static string PtrToStringUTF8(IntPtr ptr, int len)
        {
            var bytes = new byte[len];
            Marshal.Copy(ptr, bytes, 0, len);
            return EncodingUTF8.GetString(bytes);
        }

        /// <summary>
        /// Returns byte array containing UTF-8 encoding of given string.
        /// </summary>
        public static byte[] GetBytesUTF8(string str)
        {
            return EncodingUTF8.GetBytes(str);
        }

        /// <summary>
        /// Get string from a UTF8 encoded byte array.
        /// </summary>
        public static string GetStringUTF8(byte[] bytes)
        {
            return EncodingUTF8.GetString(bytes);
        }

        /// <summary>
        /// Returns byte array containing ASCII encoding of given string.
        /// </summary>
        public static byte[] GetBytesASCII(string str)
        {
            return EncodingASCII.GetBytes(str);
        }

        /// <summary>
        /// Get string from an ASCII encoded byte array.
        /// </summary>
        public static string GetStringASCII(byte[] bytes)
        {
            return EncodingASCII.GetString(bytes);
        }
    }
}
