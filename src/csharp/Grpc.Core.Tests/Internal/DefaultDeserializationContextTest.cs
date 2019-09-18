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
    public class DefaultDeserializationContextTest
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
        public void PayloadAsReadOnlySequence_ZeroSegmentPayload()
        {
            var context = new DefaultDeserializationContext();
            context.Initialize(fakeBufferReaderManager.CreateMultiSegmentBufferReader(new List<byte[]> {}));

            Assert.AreEqual(0, context.PayloadLength);

            var sequence = context.PayloadAsReadOnlySequence();

            Assert.AreEqual(ReadOnlySequence<byte>.Empty, sequence);
            Assert.IsTrue(sequence.IsEmpty);
            Assert.IsTrue(sequence.IsSingleSegment);
        }

        [TestCase(0)]
        [TestCase(1)]
        [TestCase(10)]
        [TestCase(100)]
        [TestCase(1000)]
        public void PayloadAsReadOnlySequence_SingleSegmentPayload(int segmentLength)
        {
            var origBuffer = GetTestBuffer(segmentLength);
            var context = new DefaultDeserializationContext();
            context.Initialize(fakeBufferReaderManager.CreateSingleSegmentBufferReader(origBuffer));

            Assert.AreEqual(origBuffer.Length, context.PayloadLength);

            var sequence = context.PayloadAsReadOnlySequence();

            Assert.AreEqual(origBuffer.Length, sequence.Length);
            Assert.AreEqual(origBuffer.Length, sequence.First.Length);
            Assert.IsTrue(sequence.IsSingleSegment);
            CollectionAssert.AreEqual(origBuffer, sequence.First.ToArray());
        }

        [TestCase(0, 5, 10)]
        [TestCase(1, 1, 1)]
        [TestCase(10, 100, 1000)]
        [TestCase(100, 100, 10)]
        [TestCase(1000, 1000, 1000)]
        public void PayloadAsReadOnlySequence_MultiSegmentPayload(int segmentLen1, int segmentLen2, int segmentLen3)
        {
            var origBuffer1 = GetTestBuffer(segmentLen1);
            var origBuffer2 = GetTestBuffer(segmentLen2);
            var origBuffer3 = GetTestBuffer(segmentLen3);
            int totalLen = origBuffer1.Length + origBuffer2.Length + origBuffer3.Length;

            var context = new DefaultDeserializationContext();
            context.Initialize(fakeBufferReaderManager.CreateMultiSegmentBufferReader(new List<byte[]> { origBuffer1, origBuffer2, origBuffer3 }));

            Assert.AreEqual(totalLen, context.PayloadLength);

            var sequence = context.PayloadAsReadOnlySequence();

            Assert.AreEqual(totalLen, sequence.Length);

            var segmentEnumerator = sequence.GetEnumerator();

            Assert.IsTrue(segmentEnumerator.MoveNext());
            CollectionAssert.AreEqual(origBuffer1, segmentEnumerator.Current.ToArray());

            Assert.IsTrue(segmentEnumerator.MoveNext());
            CollectionAssert.AreEqual(origBuffer2, segmentEnumerator.Current.ToArray());

            Assert.IsTrue(segmentEnumerator.MoveNext());
            CollectionAssert.AreEqual(origBuffer3, segmentEnumerator.Current.ToArray());

            Assert.IsFalse(segmentEnumerator.MoveNext());
        }

        [TestCase]
        public void NullPayloadNotAllowed()
        {
            var context = new DefaultDeserializationContext();
            Assert.Throws(typeof(InvalidOperationException), () => context.Initialize(fakeBufferReaderManager.CreateNullPayloadBufferReader()));
        }

        [TestCase]
        public void PayloadAsNewByteBuffer_ZeroSegmentPayload()
        {
            var context = new DefaultDeserializationContext();
            context.Initialize(fakeBufferReaderManager.CreateMultiSegmentBufferReader(new List<byte[]> {}));

            Assert.AreEqual(0, context.PayloadLength);

            var payload = context.PayloadAsNewBuffer();
            Assert.AreEqual(0, payload.Length);
        }

        [TestCase(0)]
        [TestCase(1)]
        [TestCase(10)]
        [TestCase(100)]
        [TestCase(1000)]
        public void PayloadAsNewByteBuffer_SingleSegmentPayload(int segmentLength)
        {
            var origBuffer = GetTestBuffer(segmentLength);
            var context = new DefaultDeserializationContext();
            context.Initialize(fakeBufferReaderManager.CreateSingleSegmentBufferReader(origBuffer));

            Assert.AreEqual(origBuffer.Length, context.PayloadLength);

            var payload = context.PayloadAsNewBuffer();
            CollectionAssert.AreEqual(origBuffer, payload);
        }

        [TestCase(0, 5, 10)]
        [TestCase(1, 1, 1)]
        [TestCase(10, 100, 1000)]
        [TestCase(100, 100, 10)]
        [TestCase(1000, 1000, 1000)]
        public void PayloadAsNewByteBuffer_MultiSegmentPayload(int segmentLen1, int segmentLen2, int segmentLen3)
        {
            var origBuffer1 = GetTestBuffer(segmentLen1);
            var origBuffer2 = GetTestBuffer(segmentLen2);
            var origBuffer3 = GetTestBuffer(segmentLen3);

            var context = new DefaultDeserializationContext();
            context.Initialize(fakeBufferReaderManager.CreateMultiSegmentBufferReader(new List<byte[]> { origBuffer1, origBuffer2, origBuffer3 }));

            var payload = context.PayloadAsNewBuffer();

            var concatenatedOrigBuffers = new List<byte>();
            concatenatedOrigBuffers.AddRange(origBuffer1);
            concatenatedOrigBuffers.AddRange(origBuffer2);
            concatenatedOrigBuffers.AddRange(origBuffer3);

            Assert.AreEqual(concatenatedOrigBuffers.Count, context.PayloadLength);
            Assert.AreEqual(concatenatedOrigBuffers.Count, payload.Length);
            CollectionAssert.AreEqual(concatenatedOrigBuffers, payload);
        }

        [TestCase]
        public void GetPayloadMultipleTimesIsIllegal()
        {
            var origBuffer = GetTestBuffer(100);
            var context = new DefaultDeserializationContext();
            context.Initialize(fakeBufferReaderManager.CreateSingleSegmentBufferReader(origBuffer));

            Assert.AreEqual(origBuffer.Length, context.PayloadLength);

            var payload = context.PayloadAsNewBuffer();
            CollectionAssert.AreEqual(origBuffer, payload);

            // Getting payload multiple times is illegal
            Assert.Throws(typeof(InvalidOperationException), () => context.PayloadAsNewBuffer());
            Assert.Throws(typeof(InvalidOperationException), () => context.PayloadAsReadOnlySequence());
        }

        [TestCase]
        public void ResetContextAndReinitialize()
        {
            var origBuffer = GetTestBuffer(100);
            var context = new DefaultDeserializationContext();
            context.Initialize(fakeBufferReaderManager.CreateSingleSegmentBufferReader(origBuffer));

            Assert.AreEqual(origBuffer.Length, context.PayloadLength);

            // Reset invalidates context
            context.Reset();

            Assert.AreEqual(0, context.PayloadLength);
            Assert.Throws(typeof(NullReferenceException), () => context.PayloadAsNewBuffer());
            Assert.Throws(typeof(NullReferenceException), () => context.PayloadAsReadOnlySequence());

            // Previously reset context can be initialized again
            var origBuffer2 = GetTestBuffer(50);
            context.Initialize(fakeBufferReaderManager.CreateSingleSegmentBufferReader(origBuffer2));

            Assert.AreEqual(origBuffer2.Length, context.PayloadLength);
            CollectionAssert.AreEqual(origBuffer2, context.PayloadAsNewBuffer());
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
