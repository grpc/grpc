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

        /// <summary>
        /// Reads metadata from this instance.
        /// </summary>
        public unsafe Metadata ReadUnsafe()
        {
            var metadataArrayP = (MetadataArray*)handle;
            return ReadMetadataFromPtrUnsafe(metadataArrayP);
        }
            
        public static MetadataArraySafeHandle Create(Metadata metadata)
        {
            using (Profilers.ForCurrentThread().NewScope("MetadataArraySafeHandle.Create"))
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
        }

        /// <summary>
        /// Reads metadata from pointer to grpc_metadata_array
        /// </summary>
        public static unsafe Metadata ReadMetadataFromPtrUnsafe(MetadataArray* metadataArrayP)
        {
            //var metadataArrayP = (MetadataArray*)metadataArrayPtr;
            long count = (long)metadataArrayP->count;
            if (count == 0)
            {
                return new Metadata();
            }

            var metadata = new Metadata();
            var metadataP = (MetadataNative*)metadataArrayP->metadata;
            for (long i = 0; i < count; i++) {
                string key = Marshal.PtrToStringAnsi(metadataP->key);

                var bytes = new byte[(long)metadataP->valueLength];
                Marshal.Copy(metadataP->value, bytes, 0, bytes.Length);
                metadata.Add (Metadata.Entry.CreateUnsafe (key, bytes));

                metadataP++;
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

        // corresponds to struct grpc_medatata
        [StructLayout(LayoutKind.Sequential)]
        internal struct MetadataNative
        {
            public IntPtr key;
            public IntPtr value;
            public UIntPtr valueLength;
            public uint flags;

            IntPtr obfuscated0;
            IntPtr obfuscated1;
            IntPtr obfuscated2;
            IntPtr obfuscated3;
        }

        // corresponds to struct grpc_medatata_array
        [StructLayout(LayoutKind.Sequential)]
        internal struct MetadataArray
        {
            public UIntPtr count;
            public UIntPtr capacity;
            public IntPtr metadata;
        }
    }
}
