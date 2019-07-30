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
using System.Buffers;
using System.Threading;

namespace Grpc.Core.Internal
{
    internal class DefaultSerializationContext : SerializationContext
    {
        static readonly ThreadLocal<DefaultSerializationContext> threadLocalInstance =
            new ThreadLocal<DefaultSerializationContext>(() => new DefaultSerializationContext(), false);

        bool isComplete;
        //byte[] payload;
        NativeBufferWriter bufferWriter;

        public DefaultSerializationContext()
        {
            Reset();
        }

        public override void Complete(byte[] payload)
        {
            GrpcPreconditions.CheckState(!isComplete);
            this.isComplete = true;

            GetBufferWriter();
            var destSpan = bufferWriter.GetSpan(payload.Length);
            payload.AsSpan().CopyTo(destSpan);
            bufferWriter.Advance(payload.Length);
            bufferWriter.Complete();
            //this.payload = payload;
        }

        /// <summary>
        /// Expose serializer as buffer writer
        /// </summary>
        public override IBufferWriter<byte> GetBufferWriter()
        {
            if (bufferWriter == null)
            {
                // TODO: avoid allocation..
                bufferWriter = new NativeBufferWriter();
            }
            return bufferWriter;
        }

        /// <summary>
        /// Complete the payload written so far.
        /// </summary>
        public override void Complete()
        {
            GrpcPreconditions.CheckState(!isComplete);
            bufferWriter.Complete();
            this.isComplete = true;
        }

        internal SliceBufferSafeHandle GetPayload()
        {
            return bufferWriter.GetSliceBuffer();
        }

        public void Reset()
        {
            this.isComplete = false;
            //this.payload = null;
            this.bufferWriter = null;
        }

        public static DefaultSerializationContext GetInitializedThreadLocal()
        {
            var instance = threadLocalInstance.Value;
            instance.Reset();
            return instance;
        }

        private class NativeBufferWriter : IBufferWriter<byte>
        {
            private SliceBufferSafeHandle sliceBuffer = SliceBufferSafeHandle.Create();

            public void Advance(int count)
            {
                sliceBuffer.Advance(count);
            }

            public Memory<byte> GetMemory(int sizeHint = 0)
            {
                // TODO: implement
                throw new NotImplementedException();
            }

            public Span<byte> GetSpan(int sizeHint = 0)
            {
                return sliceBuffer.GetSpan(sizeHint);
            }

            public void Complete()
            {
                sliceBuffer.Complete();
            }

            public SliceBufferSafeHandle GetSliceBuffer()
            {
                return sliceBuffer;
            }
        }
    }
}
