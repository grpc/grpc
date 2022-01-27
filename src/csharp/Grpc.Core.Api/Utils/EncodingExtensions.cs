#region Copyright notice and license
// Copyright 2019 The gRPC Authors
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

namespace Grpc.Core.Api.Utils
{

    internal static class EncodingExtensions
    {
#if NET45 // back-fill over a method missing in NET45
        /// <summary>
        /// Converts <c>byte*</c> pointing to an encoded byte array to a <c>string</c> using the provided <c>Encoding</c>.
        /// </summary>
        public static unsafe string GetString(this Encoding encoding, byte* source, int byteCount)
        {
            if (byteCount == 0) return ""; // most callers will have already checked, but: make sure

            // allocate a right-sized string and decode into it
            int charCount = encoding.GetCharCount(source, byteCount);
            string s = new string('\0', charCount);
            fixed (char* cPtr = s)
            {
                encoding.GetChars(source, byteCount, cPtr, charCount);
            }
            return s;
        }
#endif
        /// <summary>
        /// Converts <c>IntPtr</c> pointing to a encoded byte array to a <c>string</c> using the provided <c>Encoding</c>.
        /// </summary>
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static unsafe string GetString(this Encoding encoding, IntPtr ptr, int len)
        {
            return len == 0 ? "" : encoding.GetString((byte*)ptr.ToPointer(), len);
        }
    }

}
