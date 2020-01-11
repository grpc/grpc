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
    internal class ReusableSliceBuffer
    {
        public const int MaxCachedSegments = 1024;  // ~4MB payload for 4K slices

        readonly SliceSegment[] cachedSegments = new SliceSegment[MaxCachedSegments];
        int populatedSegmentCount;

        public ReadOnlySequence<byte> PopulateFrom(IBufferReader bufferReader)
        {
            populatedSegmentCount = 0;
            long offset = 0;
            SliceSegment prevSegment = null;
            while (bufferReader.TryGetNextSlice(out Slice slice))
            {
                // Initialize cached segment if still null or just allocate a new segment if we already reached MaxCachedSegments
                var current = populatedSegmentCount < cachedSegments.Length ? cachedSegments[populatedSegmentCount] : new SliceSegment();
                if (current == null)
                {
                    current = cachedSegments[populatedSegmentCount] = new SliceSegment();
                }

                current.Reset(slice, offset);
                prevSegment?.SetNext(current);

                populatedSegmentCount ++;
                offset += slice.Length;
                prevSegment = current;
            }

            // Not necessary for ending the ReadOnlySequence, but for making sure we
            // don't keep more than MaxCachedSegments alive.
            prevSegment?.SetNext(null);

            if (populatedSegmentCount == 0)
            {
                return ReadOnlySequence<byte>.Empty;
            }

            var firstSegment = cachedSegments[0];
            var lastSegment = prevSegment;
            return new ReadOnlySequence<byte>(firstSegment, 0, lastSegment, lastSegment.Memory.Length);
        }

        public void Invalidate()
        {
            if (populatedSegmentCount == 0)
            {
                return;
            }
            var segment = cachedSegments[0];
            while (segment != null)
            {
                segment.Reset(new Slice(IntPtr.Zero, 0), 0);
                var nextSegment = (SliceSegment) segment.Next;
                segment.SetNext(null);
                segment = nextSegment;
            }
            populatedSegmentCount = 0;
        }

        // Represents a segment in ReadOnlySequence
        // Segment is backed by Slice and the instances are reusable.
        private class SliceSegment : ReadOnlySequenceSegment<byte>
        {
            readonly SliceMemoryManager pointerMemoryManager = new SliceMemoryManager();

            public void Reset(Slice slice, long runningIndex)
            {
                pointerMemoryManager.Reset(slice);
                Memory = pointerMemoryManager.Memory;  // maybe not always necessary
                RunningIndex = runningIndex;
            }

            public void SetNext(ReadOnlySequenceSegment<byte> next)
            {
                Next = next;
            }        
        }     
    }
}
