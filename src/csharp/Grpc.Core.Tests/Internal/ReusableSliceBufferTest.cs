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
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    // Converts IBufferReader into instances of ReadOnlySequence<byte>
    // Objects representing the sequence segments are cached to decrease GC load.
    public class ReusableSliceBufferTest
    {
        FakeBufferReaderManager fakeBufferReaderManager;

        [SetUp]
        public void Init()
        {
            fakeBufferReaderManager = new FakeBufferReaderManager();
        }

        [TearDown]
        public void Cleanup()
        {
            fakeBufferReaderManager.Dispose();
        }

        [TestCase]
        public void NullPayload()
        {
            var sliceBuffer = new ReusableSliceBuffer();
            Assert.Throws(typeof(ArgumentNullException), () => sliceBuffer.PopulateFrom(fakeBufferReaderManager.CreateNullPayloadBufferReader()));
        }

        [TestCase]
        public void ZeroSegmentPayload()
        {
            var sliceBuffer = new ReusableSliceBuffer();
            var sequence = sliceBuffer.PopulateFrom(fakeBufferReaderManager.CreateMultiSegmentBufferReader(new List<byte[]> {}));

            Assert.AreEqual(ReadOnlySequence<byte>.Empty, sequence);
            Assert.IsTrue(sequence.IsEmpty);
            Assert.IsTrue(sequence.IsSingleSegment);
        }

        [TestCase]
        public void SegmentsAreCached()
        {
            var bufferSegments1 = Enumerable.Range(0, 100).Select((_) => GetTestBuffer(50)).ToList();
            var bufferSegments2 = Enumerable.Range(0, 100).Select((_) => GetTestBuffer(50)).ToList();

            var sliceBuffer = new ReusableSliceBuffer();

            var sequence1 = sliceBuffer.PopulateFrom(fakeBufferReaderManager.CreateMultiSegmentBufferReader(bufferSegments1));
            var memoryManagers1 = GetMemoryManagersForSequenceSegments(sequence1);

            sliceBuffer.Invalidate();

            var sequence2 = sliceBuffer.PopulateFrom(fakeBufferReaderManager.CreateMultiSegmentBufferReader(bufferSegments2));
            var memoryManagers2 = GetMemoryManagersForSequenceSegments(sequence2);

            // check memory managers are identical objects (i.e. they've been reused)
            CollectionAssert.AreEquivalent(memoryManagers1, memoryManagers2);
        }

        [TestCase]
        public void MultiSegmentPayload_LotsOfSegments()
        {
            var bufferSegments = Enumerable.Range(0, ReusableSliceBuffer.MaxCachedSegments + 100).Select((_) => GetTestBuffer(10)).ToList();

            var sliceBuffer = new ReusableSliceBuffer();
            var sequence = sliceBuffer.PopulateFrom(fakeBufferReaderManager.CreateMultiSegmentBufferReader(bufferSegments));

            int index = 0;
            foreach (var memory in sequence)
            {
                CollectionAssert.AreEqual(bufferSegments[index], memory.ToArray());
                index ++;
            }
        }

        [TestCase]
        public void InvalidateMakesSequenceUnusable()
        {
            var origBuffer = GetTestBuffer(100);

            var sliceBuffer = new ReusableSliceBuffer();
            var sequence = sliceBuffer.PopulateFrom(fakeBufferReaderManager.CreateMultiSegmentBufferReader(new List<byte[]> { origBuffer }));

            Assert.AreEqual(origBuffer.Length, sequence.Length);

            sliceBuffer.Invalidate();

            // Invalidate with make the returned sequence completely unusable and broken, users must not use it beyond the deserializer functions.
            Assert.Throws(typeof(ArgumentOutOfRangeException), () => { var first = sequence.First; });
        }

        private List<MemoryManager<byte>> GetMemoryManagersForSequenceSegments(ReadOnlySequence<byte> sequence)
        {
            var result = new List<MemoryManager<byte>>();
            foreach (var memory in sequence)
            {
                Assert.IsTrue(MemoryMarshal.TryGetMemoryManager(memory, out MemoryManager<byte> memoryManager));
                result.Add(memoryManager);
            }
            return result;
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
