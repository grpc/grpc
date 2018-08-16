#region Copyright notice and license

// Copyright 2018 The gRPC Authors
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
using System.Threading;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Rough equivalent of grpc_slice
    /// </summary>
    internal struct Slice
    {
        // needs to be more than GRPC_SLICE_INLINED_SIZE
        // TODO: add a test to check that this is >= sizeof(grpc_slice)

        #pragma warning disable 0618
        // We need to use the obsolete non-generic version of Marshal.SizeOf because the generic version is not available in net45
        public static readonly int InlineDataMaxLength = Marshal.SizeOf(typeof(InlineData));
        #pragma warning restore 0618

        public readonly long length;
        public readonly IntPtr sliceDataPtr;
        public readonly InlineData inlineData;

        public bool IsInline => sliceDataPtr == IntPtr.Zero;

        public Slice(long length, IntPtr sliceDataPtr, InlineData inlineData)
        {
            this.length = length;
            this.sliceDataPtr = sliceDataPtr;
            this.inlineData = inlineData;
        }

        // copies data of the slice to given span.
        // there needs to be enough space in the destination span
        public void CopyTo(Span<byte> destination)
        {
            // TODO: add some safety checks
            ReadOnlySpan<byte> span;
            if (IsInline)
            {
                // create a copy on stack so we can dereference without fixed block
                var inlineDataCopy = this.inlineData;
                unsafe { span = new Span<byte>(&inlineDataCopy, (int) this.length); }
            }
            else
            {
                unsafe { span = new ReadOnlySpan<byte>((byte*) this.sliceDataPtr, (int) this.length); }
            }
            span.CopyTo(destination);
        }

        /// <summary>
        /// Returns a <see cref="System.String"/> that represents the current <see cref="Grpc.Core.Internal.Slice"/>.
        /// </summary>
        public override string ToString()
        {
            return string.Format("[Slice: length={0}, IsInline={1}, sliceDataPtr={2}]", length, IsInline, sliceDataPtr);
        }

        internal struct InlineData
        {
            public InlineData(ulong inline0, ulong inline1, ulong inline2)
            {
                this.inline0 = inline0;
                this.inline1 = inline1;
                this.inline2 = inline2;
            }

            public readonly ulong inline0;
            public readonly ulong inline1;
            public readonly ulong inline2;
        }
    }
}
