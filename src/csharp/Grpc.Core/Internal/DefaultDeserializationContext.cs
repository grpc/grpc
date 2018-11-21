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

using Grpc.Core.Utils;
using System;
using System.Threading;
using System.Buffers;

namespace Grpc.Core.Internal
{
    internal class DefaultDeserializationContext : DeserializationContext
    {
        static readonly ThreadLocal<DefaultDeserializationContext> threadLocalInstance =
            new ThreadLocal<DefaultDeserializationContext>(() => new DefaultDeserializationContext(), false);

        IBufferReader bufferReader;

        public DefaultDeserializationContext()
        {
            Reset();
        }

        public override int PayloadLength => bufferReader.TotalLength.Value;

        public override byte[] PayloadAsNewBuffer()
        {
            if (!bufferReader.TotalLength.HasValue)
            {
                return null;
            }
            int len = bufferReader.TotalLength.Value;
            var buffer = new byte[len];
            FillContinguousBuffer(bufferReader, new Span<byte>(buffer));
            return buffer;
        }

        public void Initialize(IBufferReader bufferReader)
        {
            // TODO: populate PayloadLength early...
            this.bufferReader = GrpcPreconditions.CheckNotNull(bufferReader);
        }

        public void Reset()
        {
            this.bufferReader = null;
        }

        public override IMemoryOwner<byte> PayloadAsRentedBuffer()
        {
            if (!bufferReader.TotalLength.HasValue)
            {
                return null;
            }
            int len = bufferReader.TotalLength.Value;
            // Shared memory pool only pools arrays <= 1MB size, so messages
            // larger than that will not benefit from array pooling.
            var rentedBuffer = MemoryPool<byte>.Shared.Rent(len);
            FillContinguousBuffer(bufferReader, rentedBuffer.Memory.Span);
            return rentedBuffer;
        }

        public override bool TryGetNextBufferSegment(out ReadOnlySpan<byte> bufferSegment)
        {
            // TODO(jtattermusch): implement
            throw new NotImplementedException();
        }

        public static DefaultDeserializationContext GetInitializedThreadLocal(IBufferReader bufferReader)
        {
            var instance = threadLocalInstance.Value;
            instance.Initialize(bufferReader);
            return instance;
        }

        private static void FillContinguousBuffer(IBufferReader reader, Span<byte> destination)
        {
            int offset = 0;
            while (reader.TryGetNextSlice(out Slice slice))
            {
                slice.CopyTo(destination.Slice(offset, (int)slice.Length));
                offset += (int)slice.Length;
            }
            // check that we fill the entire destination
            GrpcPreconditions.CheckState(offset == reader.TotalLength.Value);
        }
    }
}
