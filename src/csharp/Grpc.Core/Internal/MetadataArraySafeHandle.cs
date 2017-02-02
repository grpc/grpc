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
using System.Threading.Tasks;
using Grpc.Core.Profiling;

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
        public static Metadata ReadMetadataFromPtrUnsafe(IntPtr metadataArray)
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
                string key = Marshal.PtrToStringAnsi(keyPtr, (int)keyLen.ToUInt32());
                UIntPtr valueLen;
                IntPtr valuePtr = Native.grpcsharp_metadata_array_get_value(metadataArray, index, out valueLen);
                var bytes = new byte[valueLen.ToUInt64()];
                Marshal.Copy(valuePtr, bytes, 0, bytes.Length);
                metadata.Add(Metadata.Entry.CreateUnsafe(key, bytes));
            }
            return metadata;
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
