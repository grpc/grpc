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

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class DefaultSerializationContextTest
    {
        [TestCase]
        public void CompleteAllowedOnlyOnce()
        {
            var context = new DefaultSerializationContext();
            var buffer = GetTestBuffer(10);

            context.Complete(buffer);
            Assert.Throws(typeof(InvalidOperationException), () => context.Complete(buffer));
            Assert.Throws(typeof(InvalidOperationException), () => context.Complete());
        }

        [TestCase]
        public void CompleteAllowedOnlyOnce2()
        {
            var context = new DefaultSerializationContext();

            context.Complete();
            Assert.Throws(typeof(InvalidOperationException), () => context.Complete(GetTestBuffer(10)));
            Assert.Throws(typeof(InvalidOperationException), () => context.Complete());
        }

        [TestCase(0)]
        [TestCase(1)]
        [TestCase(10)]
        [TestCase(100)]
        [TestCase(1000)]
        public void ByteArrayPayload(int payloadSize)
        {
            var context = new DefaultSerializationContext();
            var origPayload = GetTestBuffer(payloadSize);

            context.Complete(origPayload);

            var nativePayload = context.GetPayload().ToByteArray();
            CollectionAssert.AreEqual(origPayload, nativePayload);
        }

        [TestCase(0)]
        [TestCase(1)]
        [TestCase(10)]
        [TestCase(100)]
        [TestCase(1000)]
        public void BufferWriter_OneSegment(int payloadSize)
        {
            var context = new DefaultSerializationContext();
            var origPayload = GetTestBuffer(payloadSize);

            var bufferWriter = context.GetBufferWriter();
            origPayload.AsSpan().CopyTo(bufferWriter.GetSpan(payloadSize));
            bufferWriter.Advance(payloadSize);
            // TODO: test that call to Complete() is required.

            var nativePayload = context.GetPayload().ToByteArray();
            CollectionAssert.AreEqual(origPayload, nativePayload);
        }

        [TestCase(1, 4)]  // small slice size tests grpc_slice with inline data
        [TestCase(10, 4)]
        [TestCase(100, 4)]
        [TestCase(1000, 4)]
        [TestCase(1, 64)]  // larger slice size tests allocated grpc_slices
        [TestCase(10, 64)]
        [TestCase(1000, 50)]
        [TestCase(1000, 64)]
        public void BufferWriter_MultipleSegments(int payloadSize, int maxSliceSize)
        {
            var context = new DefaultSerializationContext();
            var origPayload = GetTestBuffer(payloadSize);

            var bufferWriter = context.GetBufferWriter();
            for (int offset = 0; offset < payloadSize; offset += maxSliceSize)
            {
                var sliceSize = Math.Min(maxSliceSize, payloadSize - offset);
                // we allocate last slice as too big intentionally to test that shrinking works
                var dest = bufferWriter.GetSpan(maxSliceSize);
                
                origPayload.AsSpan(offset, sliceSize).CopyTo(dest);
                bufferWriter.Advance(sliceSize);
            }
            context.Complete();
            
            var nativePayload = context.GetPayload().ToByteArray();
            CollectionAssert.AreEqual(origPayload, nativePayload);

            context.GetPayload().Dispose(); // TODO: do it better.. (use the scope...)
        }

        // AdjustTailSpace(0) if previous tail size is 0....

        // test that context.Complete() call is required...

        // BufferWriter.GetMemory... (add refs to the original memory?)

        // TODO: set payload and then get IBufferWriter should throw?

        // TODO: test Reset()...

        // TODO: destroy SliceBufferSafeHandles... (use usagescope...)




        private byte[] GetTestBuffer(int length)
        {
            var testBuffer = new byte[length];
            for (int i = 0; i < testBuffer.Length; i++)
            {
                testBuffer[i] = (byte) i;
            }
            return testBuffer;
        }
    }
}
