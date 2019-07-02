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
    /// grpc_metadata_array from <c>grpc/grpc.h</c>
    /// </summary>
    internal class MetadataArraySafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        private MetadataArraySafeHandle()
        {
        }
            
        public static MetadataArraySafeHandle Create(Metadata metadata)
        {
            if (metadata.Count == 0)
            {
                return new MetadataArraySafeHandle();
            }

            // TODO(jtattermusch): we might wanna check that the metadata is readonly
            var metadataArray = Native.grpcsharp_metadata_array_create(new UIntPtr((ulong)metadata.Count));
            for (int i = 0; i < metadata.Count; i++)
            {
                var valueBytes = metadata[i].GetSerializedValueUnsafe();
                Native.grpcsharp_metadata_array_add(metadataArray, metadata[i].Key, valueBytes, new UIntPtr((ulong)valueBytes.Length));
            }
            return metadataArray;
        }

        /// <summary>
        /// Reads metadata from pointer to grpc_metadata_array
        /// </summary>
        public static unsafe Metadata ReadMetadataFromPtrUnsafe(IntPtr metadataArray)
        {
            if (metadataArray == IntPtr.Zero)
            {
                return null;
            }

            ulong count = Native.grpcsharp_metadata_array_count(metadataArray).ToUInt64();

            var metadata = new Metadata();
            for (ulong i = 0; i < count; i++)
            {
                var index = new UIntPtr(i);
                UIntPtr keyLen;
                IntPtr keyPtr = Native.grpcsharp_metadata_array_get_key(metadataArray, index, out keyLen);
                int keyLen32 = checked((int)keyLen.ToUInt32());
                string key = WellKnownStrings.TryIdentify((byte*)keyPtr.ToPointer(), keyLen32)
                    ?? Marshal.PtrToStringAnsi(keyPtr, keyLen32);
                UIntPtr valueLen;
                IntPtr valuePtr = Native.grpcsharp_metadata_array_get_value(metadataArray, index, out valueLen);
                int len32 = checked((int)valueLen.ToUInt64());
                metadata.Add(Metadata.Entry.CreateUnsafe(key, (byte*)valuePtr.ToPointer(), len32));
            }
            return metadata;
        }

        private static class WellKnownStrings
        {
            // turn a string of ASCII-length 8 into a ulong using the CPUs current
            // endianness; this allows us to do the same thing in TryIdentify,
            // testing string prefixes (of length >= 8) in a single load/compare
            private static unsafe ulong Thunk8(string value)
            {
                int byteCount = Encoding.ASCII.GetByteCount(value);
                if (byteCount != 8) throw new ArgumentException(nameof(value));
                ulong result = 0;
                fixed (char* cPtr = value)
                {
                    Encoding.ASCII.GetBytes(cPtr, value.Length, (byte*)&result, byteCount);
                }
                return result;
            }
            private static readonly ulong UserAgent = Thunk8("user-age");
            public static unsafe string TryIdentify(byte* source, int length)
            {
                switch (length)
                {
                    case 10:
                        // test the first 8 bytes via evilness
                        ulong first8 = *(ulong*)source;
                        if (first8 == UserAgent & source[8] == (byte)'n' & source[9] == (byte)'t')
                            return "user-agent";

                        break;
                }
                return null;
            }
        }

        internal IntPtr Handle
        {
            get
            {
                return handle;
            }
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_metadata_array_destroy_full(handle);
            return true;
        }
    }
}
