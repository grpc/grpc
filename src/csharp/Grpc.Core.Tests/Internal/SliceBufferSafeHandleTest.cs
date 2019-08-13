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
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class SliceBufferSafeHandleTest
    {
        [TestCase]
        public void Complete_EmptyBuffer()
        {
            using (var sliceBuffer = SliceBufferSafeHandle.Create())
            {
                sliceBuffer.Complete();
                CollectionAssert.AreEqual(new byte[0], sliceBuffer.ToByteArray());
            }
        }

        [TestCase]
        public void Complete_TailSizeZero()
        {
            using (var sliceBuffer = SliceBufferSafeHandle.Create())
            {
                var origPayload = GetTestBuffer(10);
                origPayload.AsSpan().CopyTo(sliceBuffer.GetSpan(origPayload.Length));
                sliceBuffer.Advance(origPayload.Length);
                // call complete where tail space size == 0
                sliceBuffer.Complete();
                CollectionAssert.AreEqual(origPayload, sliceBuffer.ToByteArray());
            }
        }

        [TestCase]
        public void Complete_TruncateTailSpace()
        {
            using (var sliceBuffer = SliceBufferSafeHandle.Create())
            {
                var origPayload = GetTestBuffer(10);
                var dest = sliceBuffer.GetSpan(origPayload.Length + 10);
                origPayload.AsSpan().CopyTo(dest);
                sliceBuffer.Advance(origPayload.Length);
                // call complete where tail space needs to be truncated
                sliceBuffer.Complete();
                CollectionAssert.AreEqual(origPayload, sliceBuffer.ToByteArray());
            }
        }

        [TestCase]
        public void SliceBufferIsReusable()
        {
            using (var sliceBuffer = SliceBufferSafeHandle.Create())
            {
                var origPayload = GetTestBuffer(10);
                origPayload.AsSpan().CopyTo(sliceBuffer.GetSpan(origPayload.Length));
                sliceBuffer.Advance(origPayload.Length);
                sliceBuffer.Complete();
                CollectionAssert.AreEqual(origPayload, sliceBuffer.ToByteArray());

                sliceBuffer.Reset();

                var origPayload2 = GetTestBuffer(20);
                origPayload2.AsSpan().CopyTo(sliceBuffer.GetSpan(origPayload2.Length));
                sliceBuffer.Advance(origPayload2.Length);
                sliceBuffer.Complete();
                CollectionAssert.AreEqual(origPayload2, sliceBuffer.ToByteArray());

                sliceBuffer.Reset();

                CollectionAssert.AreEqual(new byte[0], sliceBuffer.ToByteArray());
            }
        }

        [TestCase]
        public void SliceBuffer_SizeHintZero()
        {
            using (var sliceBuffer = SliceBufferSafeHandle.Create())
            {
                var destSpan = sliceBuffer.GetSpan(0);
                Assert.IsTrue(destSpan.Length > 0);  // some non-zero size memory is made available

                sliceBuffer.Reset();

                var destMemory = sliceBuffer.GetMemory(0);
                Assert.IsTrue(destMemory.Length > 0);
            }
        }

        [TestCase(0)]
        [TestCase(1000)]
        public void SliceBuffer_BigPayload(int sizeHint)
        {
            using (var sliceBuffer = SliceBufferSafeHandle.Create())
            {
                var bigPayload = GetTestBuffer(4 * 1024 * 1024);

                int offset = 0;
                while (offset < bigPayload.Length)
                {
                    var destSpan = sliceBuffer.GetSpan(sizeHint);
                    int copySize = Math.Min(destSpan.Length, bigPayload.Length - offset);
                    bigPayload.AsSpan(offset, copySize).CopyTo(destSpan);
                    sliceBuffer.Advance(copySize);
                    offset += copySize;
                }
                
                sliceBuffer.Complete();
                CollectionAssert.AreEqual(bigPayload, sliceBuffer.ToByteArray());
            }
        }

        [TestCase]
        public void SliceBuffer_NegativeSizeHint()
        {
            using (var sliceBuffer = SliceBufferSafeHandle.Create())
            {
                Assert.Throws(typeof(ArgumentException), () => sliceBuffer.GetSpan(-1));
                Assert.Throws(typeof(ArgumentException), () => sliceBuffer.GetMemory(-1));
            }
        }

        [TestCase]
        public void SliceBuffer_AdvanceBadArg()
        {
            using (var sliceBuffer = SliceBufferSafeHandle.Create())
            {
                int size = 10;
                var destSpan = sliceBuffer.GetSpan(size);
                Assert.Throws(typeof(ArgumentException), () => sliceBuffer.Advance(size + 1));
                Assert.Throws(typeof(ArgumentException), () => sliceBuffer.Advance(-1));
            }
        }

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
