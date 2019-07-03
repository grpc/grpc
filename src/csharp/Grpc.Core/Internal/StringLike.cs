using System;
using System.Runtime.CompilerServices;
using System.Text;
using System.Runtime.InteropServices;
using System.Buffers;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// A string-like value that can be used to compare values *without materializing them as strings*
    /// </summary>
    internal unsafe struct StringLike : IEquatable<StringLike>
    {
        private readonly string knownValue;
        private readonly byte[] bytes;
        private readonly int length, hashCode;

        private static readonly Encoding Encoding = Encoding.UTF8;

        /// <summary>
        /// Create a string-like value over a string
        /// </summary>
        public static StringLike Create(string value)
        {
            if (string.IsNullOrEmpty(value))
            {
                return default(StringLike);
            }
            else
            {
                var arr = Encoding.GetBytes(value);
                return new StringLike(value, arr, arr.Length);
            }
        }

        /// <summary>
        /// Create a string-like value over a FIXED pointer using a pooled buffer
        /// buffer
        /// </summary>
        public static StringLike Rent(IntPtr pointer, int length)
        {
            return Rent((byte*)pointer.ToPointer(), length);
        }

        /// <summary>
        /// Create a string-like value over a FIXED pointer using a pooled buffer
        /// buffer
        /// </summary>
        public static StringLike Rent(byte* pointer, int length)
        {
            if (length == 0)
            {
                return default(StringLike);
            }
            else
            {
                byte[] arr = ArrayPool<byte>.Shared.Rent(length);
                Marshal.Copy(new IntPtr(pointer), arr, 0, length);
                return new StringLike(null, arr, length);
            }
        }

        /// <summary>
        /// Release the buffer, if it was pooled; it is the caller's responsibility
        /// to only do this once, and not use the value after this point
        /// </summary>
        public void Recycle()
        {
            if (IsPooled) ArrayPool<byte>.Shared.Return(bytes);
        }

        private StringLike(string knownValue, byte[] bytes, int length)
        {
            this.knownValue = knownValue;
            this.length = length;
            this.bytes = bytes;
            hashCode = GetHashCode(bytes, length);
        }

        public override int GetHashCode()
        {
            return hashCode;
        }
        public override bool Equals(object obj)
        {
            return obj is StringLike && Equals((StringLike)obj);
        }

        public override string ToString()
        {
            return knownValue ?? (length == 0 ? "" : Encoding.GetString(bytes, 0, length));
        }

        public int ByteCount { get { return length; } }

        public bool IsPooled { get { return knownValue == null & length != 0; } }

        // this **CAN BE NULL** if we didn't start from a string
        public string KnownValue {  get { return knownValue; } }

        public bool Equals(StringLike other)
        {
            return (this.length == other.length & this.hashCode == other.hashCode)
                && (this.length == 0 || BytesEqual(this.bytes, other.bytes, this.length));
        }

        private static bool BytesEqual(byte[] x, byte[] y, int length)
        {
            fixed (byte* xPtr = x, yPtr = y)
            {
                byte* a = xPtr, b = yPtr;
                if (length >= 8)
                {
                    ulong* uA = (ulong*)a, uB = (ulong*)b;
                    do
                    {
                        if (*uA++ != *uB++) return false;
                        length -= 8;
                    } while (length >= 8);
                    a = (byte*)uA;
                    b = (byte*)uB;
                }
                while (length != 0)
                {
                    if (*a++ != *b++) return false;
                    length--;
                }
                return true;
            }
        }


        // From System.Web.Util.HashCodeCombiner
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        internal static int CombineHashCodes(int h1, int h2)
        {
            return (((h1 << 5) + h1) ^ h2);
        }
        private static int GetHashCode(byte[] arr, int length)
        {
            fixed (byte* arrPtr = arr)
            {
                byte* ptr = arrPtr;
                int hashCode = length;
                if (length >= 8)
                {
                    ulong* uPtr = (ulong*)ptr;
                    do
                    {
                        hashCode = CombineHashCodes((*uPtr++).GetHashCode(), hashCode);
                        length -= 8;
                    } while (length >= 8);
                    ptr = (byte*)uPtr;
                }
                while (length != 0)
                {
                    hashCode = CombineHashCodes((*ptr++).GetHashCode(), hashCode);
                    length--;
                }
                return hashCode;
            }
        }

        internal sealed class Boxed
        {
            public readonly StringLike Value;
            public Boxed(StringLike value)
            {
                Value = value;
            }
        }

        internal Boxed Box()
        {
            // if we have an empty string, or one that is created from a string, then
            // we can use that directly; otherwise, we'll need a reliable clone
            return new Boxed((length == 0 | !IsPooled) ? this : Create(ToString()));
        }
    }
}
