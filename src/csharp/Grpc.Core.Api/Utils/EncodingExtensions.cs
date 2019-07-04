using System;
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
        public static unsafe string GetString(this Encoding encoding, IntPtr ptr, int len)
        {
            return len == 0 ? "" : encoding.GetString((byte*)ptr.ToPointer(), len);
        }
    }

}
