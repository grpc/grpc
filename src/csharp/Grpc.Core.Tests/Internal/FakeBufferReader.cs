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

            int sliceLen = Math.Min(Slice.InlineDataMaxLength, data.Length - currentIndex);
            // returning inlined slices is not very efficient, but good enough for tests
            slice = Slice.CreateInlineFrom(new ReadOnlySpan<byte>(data, currentIndex, sliceLen));
            currentIndex += sliceLen;
            return true;
        }
    }
}
