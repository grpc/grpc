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
using System.Collections.Generic;
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal.Tests
{
    internal class FakeBufferReader : IBufferReader
    {
        byte[] data;
        int currentIndex;

        public FakeBufferReader(byte[] data)
        {
            this.data = data;
            this.currentIndex = 0;
        }

        public int? TotalLength => data != null ? (int?) data.Length : null;

        public bool TryGetNextSlice(out Slice slice)
        {
            GrpcPreconditions.CheckNotNull(data);
            if (currentIndex >= data.Length)
            {
                slice = default(Slice);
                return false;
            }

            // return slice with inlined data of size 1
            // not very efficient, but enough for what we need for tests
            slice = ReadInlineSliceFromBuffer(data, currentIndex);
            currentIndex += (int) slice.Length;
            return true;
        }

        // Reads up to Slice.InlineDataMaxLength bytes from buffer.
        private static Slice ReadInlineSliceFromBuffer(byte[] buffer, int offset)
        {
            GrpcPreconditions.CheckArgument(offset >= 0);
            ulong inline0 = 0;
            ulong inline1 = 0;
            ulong inline2 = 0;

            if (offset < buffer.Length)
            {
                inline0 = BitConverter.ToUInt64(buffer, offset);
            }
            if (offset + 8 < buffer.Length)
            {
                inline1 = BitConverter.ToUInt64(buffer, offset + 8);
            }
            if (offset + 16 < buffer.Length)
            {
                inline2 = BitConverter.ToUInt64(buffer, offset + 16);
            }
            long sliceLen = Math.Min(Slice.InlineDataMaxLength, buffer.Length - offset);
            return new Slice(sliceLen, IntPtr.Zero, new Slice.InlineData(inline0, inline1, inline2));
        }
    }
}
