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
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class FakeBufferReaderManagerTest
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
            var fakeBufferReader = fakeBufferReaderManager.CreateNullPayloadBufferReader();
            Assert.IsFalse(fakeBufferReader.TotalLength.HasValue);
            Assert.Throws(typeof(ArgumentNullException), () => fakeBufferReader.TryGetNextSlice(out Slice slice));
        }
        [TestCase]
        public void ZeroSegmentPayload()
        {
            var fakeBufferReader = fakeBufferReaderManager.CreateMultiSegmentBufferReader(new List<byte[]> {});
            Assert.AreEqual(0, fakeBufferReader.TotalLength.Value);
            Assert.IsFalse(fakeBufferReader.TryGetNextSlice(out Slice slice));
        }

        [TestCase(0)]
        [TestCase(1)]
        [TestCase(10)]
        [TestCase(30)]
        [TestCase(100)]
        [TestCase(1000)]
        public void SingleSegmentPayload(int bufferLen)
        {
            var origBuffer = GetTestBuffer(bufferLen);
            var fakeBufferReader = fakeBufferReaderManager.CreateSingleSegmentBufferReader(origBuffer);
            Assert.AreEqual(origBuffer.Length, fakeBufferReader.TotalLength.Value);

            Assert.IsTrue(fakeBufferReader.TryGetNextSlice(out Slice slice));
            AssertSliceDataEqual(origBuffer, slice);

            Assert.IsFalse(fakeBufferReader.TryGetNextSlice(out Slice slice2));
        }

        [TestCase(0, 5, 10)]
        [TestCase(1, 1, 1)]
        [TestCase(10, 100, 1000)]
        [TestCase(100, 100, 10)]
        [TestCase(1000, 1000, 1000)]
        public void MultiSegmentPayload(int segmentLen1, int segmentLen2, int segmentLen3)
        {
            var origBuffer1 = GetTestBuffer(segmentLen1);
            var origBuffer2 = GetTestBuffer(segmentLen2);
            var origBuffer3 = GetTestBuffer(segmentLen3);
            var fakeBufferReader = fakeBufferReaderManager.CreateMultiSegmentBufferReader(new List<byte[]> { origBuffer1, origBuffer2, origBuffer3 });

            Assert.AreEqual(origBuffer1.Length + origBuffer2.Length + origBuffer3.Length, fakeBufferReader.TotalLength.Value);

            Assert.IsTrue(fakeBufferReader.TryGetNextSlice(out Slice slice1));
            AssertSliceDataEqual(origBuffer1, slice1);

            Assert.IsTrue(fakeBufferReader.TryGetNextSlice(out Slice slice2));
            AssertSliceDataEqual(origBuffer2, slice2);

            Assert.IsTrue(fakeBufferReader.TryGetNextSlice(out Slice slice3));
            AssertSliceDataEqual(origBuffer3, slice3);

            Assert.IsFalse(fakeBufferReader.TryGetNextSlice(out Slice slice4));
        }

        private void AssertSliceDataEqual(byte[] expected, Slice actual)
        {
            var actualSliceData = new byte[actual.Length];
            actual.ToSpanUnsafe().CopyTo(actualSliceData);
            CollectionAssert.AreEqual(expected, actualSliceData);
        }

        // create a buffer of given size and fill it with some data
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
