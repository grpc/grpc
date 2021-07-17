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
using System.Buffers;
using System.Threading;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal class DefaultDeserializationContext : DeserializationContext
    {
        static readonly ThreadLocal<DefaultDeserializationContext> threadLocalInstance =
            new ThreadLocal<DefaultDeserializationContext>(() => new DefaultDeserializationContext(), false);

        IBufferReader bufferReader;
        int payloadLength;
        ReusableSliceBuffer cachedSliceBuffer = new ReusableSliceBuffer();

        public DefaultDeserializationContext()
        {
            Reset();
        }

        public override int PayloadLength => payloadLength;

        public override byte[] PayloadAsNewBuffer()
        {
            var buffer = new byte[payloadLength];
            PayloadAsReadOnlySequence().CopyTo(buffer);
            return buffer;
        }

        public override ReadOnlySequence<byte> PayloadAsReadOnlySequence()
        {
            var sequence = cachedSliceBuffer.PopulateFrom(bufferReader);
            GrpcPreconditions.CheckState(sequence.Length == payloadLength);
            return sequence;
        }

        public void Initialize(IBufferReader bufferReader)
        {
            this.bufferReader = GrpcPreconditions.CheckNotNull(bufferReader);
            this.payloadLength = bufferReader.TotalLength.Value;  // payload must not be null
        }

        public void Reset()
        {
            this.bufferReader = null;
            this.payloadLength = 0;
            this.cachedSliceBuffer.Invalidate();
        }

        public static DefaultDeserializationContext GetInitializedThreadLocal(IBufferReader bufferReader)
        {
            var instance = threadLocalInstance.Value;
            instance.Initialize(bufferReader);
            return instance;
        }
    }
}
