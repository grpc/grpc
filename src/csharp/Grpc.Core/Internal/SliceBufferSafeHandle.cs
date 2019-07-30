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
using System.Runtime.InteropServices;
using Grpc.Core;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_slice_buffer
    /// </summary>
    internal class SliceBufferSafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<SliceBufferSafeHandle>();

        private IntPtr tailSpacePtr;
        private UIntPtr tailSpaceLen;
        

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
            GrpcPreconditions.CheckArgument(tailSpaceLen.ToUInt64() >= (ulong)count);
            tailSpaceLen = new UIntPtr(tailSpaceLen.ToUInt64() - (ulong)count);
            tailSpacePtr += count;
        }

        public unsafe Span<byte> GetSpan(int sizeHint)
        {
            GrpcPreconditions.CheckArgument(sizeHint >= 0);
            if (tailSpaceLen.ToUInt64() < (ulong) sizeHint)
            {
                // TODO: should we ignore the hint sometimes when
                // available tail space is close enough to the sizeHint?
                AdjustTailSpace(sizeHint);
            }
            return new Span<byte>(tailSpacePtr.ToPointer(), (int) tailSpaceLen.ToUInt64());
        }

        public void Complete()
        {
            AdjustTailSpace(0);
        }

        public byte[] GetPayload()
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
        // TODO: converting contents to byte[]

        // Gets data of server_rpc_new completion.
        private void AdjustTailSpace(int requestedSize)
        {
            GrpcPreconditions.CheckArgument(requestedSize >= 0);
            var requestedTailSpaceLen = new UIntPtr((ulong) requestedSize);
            tailSpacePtr = Native.grpcsharp_slice_buffer_adjust_tail_space(this, tailSpaceLen, requestedTailSpaceLen);
            tailSpaceLen = requestedTailSpaceLen;
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_slice_buffer_destroy(handle);
            return true;
        }
    }
}
