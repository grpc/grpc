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
using System.Buffers;
using System.Runtime.InteropServices;
using Grpc.Core;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Represents grpc_slice_buffer with some extra utility functions to allow
    /// writing data to it using the <c>IBufferWriter</c> interface.
    /// </summary>
    internal class SliceBufferSafeHandle : SafeHandleZeroIsInvalid, IBufferWriter<byte>
    {
        const int DefaultTailSpaceSize = 4096;  // default buffer to allocate if no size hint is provided
        static readonly NativeMethods Native = NativeMethods.Get();
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<SliceBufferSafeHandle>();

        public static readonly SliceBufferSafeHandle NullInstance = new SliceBufferSafeHandle();

        private IntPtr tailSpacePtr;
        private int tailSpaceLen;

        private SliceMemoryManager memoryManagerLazy;

        private SliceBufferSafeHandle()
        {
        }

        public static SliceBufferSafeHandle Create()
        {
            return Native.grpcsharp_slice_buffer_create();
        }

        public IntPtr Handle
        {
            get
            {
                return handle;
            }
        }

        public void Advance(int count)
        {
            GrpcPreconditions.CheckArgument(count >= 0);
            GrpcPreconditions.CheckArgument(tailSpacePtr != IntPtr.Zero || count == 0);
            GrpcPreconditions.CheckArgument(tailSpaceLen >= count);
            tailSpaceLen = tailSpaceLen - count;
            tailSpacePtr += count;
            memoryManagerLazy?.Reset();
        }

        // provides access to the "tail space" of this buffer.
        // Use GetSpan when possible for better efficiency.
        public Memory<byte> GetMemory(int sizeHint = 0)
        {
            EnsureBufferSpace(sizeHint);
            if (memoryManagerLazy == null)
            {
                memoryManagerLazy = new SliceMemoryManager();
            }
            memoryManagerLazy.Reset(new Slice(tailSpacePtr, tailSpaceLen));
            return memoryManagerLazy.Memory;
        }

        // provides access to the "tail space" of this buffer.
        public unsafe Span<byte> GetSpan(int sizeHint = 0)
        {
            EnsureBufferSpace(sizeHint);
            return new Span<byte>(tailSpacePtr.ToPointer(), tailSpaceLen);
        }

        public void Complete()
        {
            AdjustTailSpace(0);
        }

        // resets the data contained by this slice buffer
        public void Reset()
        {
            // deletes all the data in the slice buffer
            tailSpacePtr = IntPtr.Zero;
            tailSpaceLen = 0;
            memoryManagerLazy?.Reset();
            Native.grpcsharp_slice_buffer_reset_and_unref(this);
        }

        // copies the content of the slice buffer to a newly allocated byte array
        // Note that this method has a relatively high overhead and should maily be used for testing.
        public byte[] ToByteArray()
        {
            ulong sliceCount = Native.grpcsharp_slice_buffer_slice_count(this).ToUInt64();

            Slice[] slices = new Slice[sliceCount];
            int totalLen = 0;
            for (int i = 0; i < (int) sliceCount; i++)
            {
                Native.grpcsharp_slice_buffer_slice_peek(this, new UIntPtr((ulong) i), out UIntPtr sliceLen, out IntPtr dataPtr);
                slices[i] = new Slice(dataPtr, (int) sliceLen.ToUInt64());
                totalLen += (int) sliceLen.ToUInt64();

            }
            var result = new byte[totalLen];
            int offset = 0;
            for (int i = 0; i < (int) sliceCount; i++)
            {
                slices[i].ToSpanUnsafe().CopyTo(result.AsSpan(offset, slices[i].Length));
                offset += slices[i].Length;
            }
            GrpcPreconditions.CheckState(totalLen == offset);
            return result;
        }

        private void EnsureBufferSpace(int sizeHint)
        {
            GrpcPreconditions.CheckArgument(sizeHint >= 0);
            if (sizeHint == 0)
            {
                // if no hint is provided, keep the available space within some "reasonable" boundaries.
                // This is quite a naive approach which could use some fine-tuning, but currently in most case we know
                // the required buffer size in advance anyway, so this approach seems good enough for now.
                if (tailSpaceLen < DefaultTailSpaceSize / 2)
                {
                    AdjustTailSpace(DefaultTailSpaceSize);
                }
            }
            else if (tailSpaceLen < sizeHint)
            {
                // if hint is provided, always make sure we provide at least that much space
                AdjustTailSpace(sizeHint);
            }
        }

        // make sure there's exactly requestedSize bytes of continguous buffer space at the end of this slice buffer
        private void AdjustTailSpace(int requestedSize)
        {
            GrpcPreconditions.CheckArgument(requestedSize >= 0);
            tailSpacePtr = Native.grpcsharp_slice_buffer_adjust_tail_space(this, new UIntPtr((ulong) tailSpaceLen), new UIntPtr((ulong) requestedSize));
            tailSpaceLen = requestedSize;
        }
        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_slice_buffer_destroy(handle);
            return true;
        }
    }
}
