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
using System.Runtime.CompilerServices;
using System.Text;
using Grpc.Core.Api.Utils;

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
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static string PtrToStringUTF8(IntPtr ptr, int len)
        {
            return EncodingUTF8.GetString(ptr, len);
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
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static int GetMaxByteCountUTF8(string str)
        {
            return EncodingUTF8.GetMaxByteCount(str.Length);
        }

        /// <summary>
        /// Returns the actual number of bytes required to encode a given string.
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static int GetByteCountUTF8(string str)
        {
            return EncodingUTF8.GetByteCount(str);
        }
    }
}
