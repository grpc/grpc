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
using System.Runtime.InteropServices;
using System.Threading.Tasks;

using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal.Tests
{
    // Creates instances of fake IBufferReader. All created instances will become invalid once Dispose is called.
    internal class FakeBufferReaderManager : IDisposable
    {
        List<GCHandle> pinnedHandles = new List<GCHandle>();
        bool disposed = false;
        public IBufferReader CreateSingleSegmentBufferReader(byte[] data)
        {
            return CreateMultiSegmentBufferReader(new List<byte[]> { data });
        }

        public IBufferReader CreateMultiSegmentBufferReader(IEnumerable<byte[]> dataSegments)
        {
            GrpcPreconditions.CheckState(!disposed);
            GrpcPreconditions.CheckNotNull(dataSegments);
            var segments = new List<GCHandle>();
            foreach (var data in dataSegments)
            {
                GrpcPreconditions.CheckNotNull(data);
                segments.Add(GCHandle.Alloc(data, GCHandleType.Pinned));
            }
            pinnedHandles.AddRange(segments);  // all the allocated GCHandles will be freed on Dispose()
            return new FakeBufferReader(segments);
        }

        public IBufferReader CreateNullPayloadBufferReader()
        {
            GrpcPreconditions.CheckState(!disposed);
            return new FakeBufferReader(null);
        }

        public void Dispose()
        {
            if (!disposed)
            {
                disposed = true;
                for (int i = 0; i < pinnedHandles.Count; i++)
                {
                    pinnedHandles[i].Free();
                }
            }
        }

        private class FakeBufferReader : IBufferReader
        {
            readonly List<GCHandle> bufferSegments;
            readonly int? totalLength;
            readonly IEnumerator<GCHandle> segmentEnumerator;

            public FakeBufferReader(List<GCHandle> bufferSegments)
            {
                this.bufferSegments = bufferSegments;
                this.totalLength = ComputeTotalLength(bufferSegments);
                this.segmentEnumerator = bufferSegments?.GetEnumerator();
            }

            public int? TotalLength => totalLength;

            public bool TryGetNextSlice(out Slice slice)
            {
                GrpcPreconditions.CheckNotNull(bufferSegments);
                if (!segmentEnumerator.MoveNext())
                {
                    slice = default(Slice);
                    return false;
                }

                var segment = segmentEnumerator.Current;
                int sliceLen = ((byte[]) segment.Target).Length;
                slice = new Slice(segment.AddrOfPinnedObject(), sliceLen);
                return true;
            }

            static int? ComputeTotalLength(List<GCHandle> bufferSegments)
            {
                if (bufferSegments == null)
                {
                    return null;
                }

                int sum = 0;
                foreach (var segment in bufferSegments)
                {
                    var data = (byte[]) segment.Target;
                    sum += data.Length;
                }
                return sum;
            }
        }
    }
}
