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
