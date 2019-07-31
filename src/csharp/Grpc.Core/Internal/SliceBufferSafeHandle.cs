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
        static readonly NativeMethods Native = NativeMethods.Get();
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<SliceBufferSafeHandle>();

        public static readonly SliceBufferSafeHandle NullInstance = new SliceBufferSafeHandle();

        private IntPtr tailSpacePtr;
        private int tailSpaceLen;

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
        }

        // provides access to the "tail space" of this buffer.
        // Use GetSpan when possible for better efficiency.
        public Memory<byte> GetMemory(int sizeHint = 0)
        {
            // TODO: implement
            throw new NotImplementedException();
        }

        // provides access to the "tail space" of this buffer.
        public unsafe Span<byte> GetSpan(int sizeHint = 0)
        {
            GrpcPreconditions.CheckArgument(sizeHint >= 0);
            if (tailSpaceLen < sizeHint)
            {
                // TODO: should we ignore the hint sometimes when
                // available tail space is close enough to the sizeHint?
                AdjustTailSpace(sizeHint);
            }
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

        // Gets data of server_rpc_new completion.
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
