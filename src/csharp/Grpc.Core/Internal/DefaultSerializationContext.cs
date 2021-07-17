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
        SliceBufferSafeHandle sliceBuffer = SliceBufferSafeHandle.Create();

        public DefaultSerializationContext()
        {
            Reset();
        }

        public override void Complete(byte[] payload)
        {
            GrpcPreconditions.CheckState(!isComplete);
            this.isComplete = true;

            var destSpan = sliceBuffer.GetSpan(payload.Length);
            payload.AsSpan().CopyTo(destSpan);
            sliceBuffer.Advance(payload.Length);
            sliceBuffer.Complete();
        }

        /// <summary>
        /// Expose serializer as buffer writer
        /// </summary>
        public override IBufferWriter<byte> GetBufferWriter()
        {
            GrpcPreconditions.CheckState(!isComplete);
            return sliceBuffer;
        }

        public override void SetPayloadLength(int payloadLength)
        {
            // Length is calculated using the buffer writer
        }

        /// <summary>
        /// Complete the payload written so far.
        /// </summary>
        public override void Complete()
        {
            GrpcPreconditions.CheckState(!isComplete);
            sliceBuffer.Complete();
            this.isComplete = true;
        }

        internal SliceBufferSafeHandle GetPayload()
        {
            if (!isComplete)
            {
                // mimic the legacy behavior when byte[] was used to represent the payload.
                throw new NullReferenceException("No payload was set. Complete() needs to be called before payload can be used.");
            }
            return sliceBuffer;
        }

        public void Reset()
        {
            this.isComplete = false;
            this.sliceBuffer.Reset();
        }

        // Get a cached thread local instance of deserialization context
        // and wrap it in a disposable struct that allows easy resetting
        // via "using" statement.
        public static UsageScope GetInitializedThreadLocalScope()
        {
            var instance = threadLocalInstance.Value;
            return new UsageScope(instance);
        }

        public struct UsageScope : IDisposable
        {
            readonly DefaultSerializationContext context;

            public UsageScope(DefaultSerializationContext context)
            {
                this.context = context;
            }

            public DefaultSerializationContext Context => context;
            public void Dispose()
            {
                context.Reset();
            }
        }
    }
}
