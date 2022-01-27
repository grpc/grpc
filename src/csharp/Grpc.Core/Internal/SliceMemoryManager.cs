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

using Grpc.Core.Utils;
using System;
using System.Threading;

using System.Buffers;

namespace Grpc.Core.Internal
{
    // Allow creating instances of Memory<byte> from Slice.
    // Represents a chunk of native memory, but doesn't manage its lifetime.
    // Instances of this class are reuseable - they can be reset to point to a different memory chunk.
    // That is important to make the instances cacheable (rather then creating new instances
    // the old ones will be reused to reduce GC pressure).
    internal class SliceMemoryManager : MemoryManager<byte>
    {
        private Slice slice;

        public void Reset(Slice slice)
        {
            this.slice = slice;
        }

        public void Reset()
        {
            Reset(new Slice(IntPtr.Zero, 0));
        }

        public override Span<byte> GetSpan()
        {
            return slice.ToSpanUnsafe();
        }

        public override MemoryHandle Pin(int elementIndex = 0)
        {
            throw new NotSupportedException();
        }

        public override void Unpin()
        {
        }

        protected override void Dispose(bool disposing)
        {
            // NOP
        }
    }
}
