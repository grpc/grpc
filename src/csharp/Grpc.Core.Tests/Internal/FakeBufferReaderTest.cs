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
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class FakeBufferReaderTest
    {
        [TestCase(0)]
        [TestCase(1)]
        [TestCase(10)]
        [TestCase(30)]
        [TestCase(100)]
        [TestCase(1000)]
        public void BasicTest(int bufferLen)
        {
            var origBuffer = GetTestBuffer(bufferLen);
            var fakeBufferReader = new FakeBufferReader(origBuffer);
            Assert.AreEqual(origBuffer.Length, fakeBufferReader.TotalLength.Value);

            int bytesRead = 0;
            while (fakeBufferReader.TryGetNextSlice(out Slice slice))
            {
                int sliceLen = (int) slice.Length;
                var sliceData = new byte[sliceLen];
                slice.CopyTo(sliceData);
                for (int i = 0; i < sliceLen; i++)
                {
                    Assert.AreEqual(origBuffer[bytesRead + i], sliceData[i]);
                }
                bytesRead += sliceLen;
            }
            Assert.AreEqual(origBuffer.Length, bytesRead);
            Assert.IsFalse(fakeBufferReader.TryGetNextSlice(out Slice slice2));

        }

        [TestCase]
        public void NullPayload()
        {
            var fakeBufferReader = new FakeBufferReader(null);
            Assert.IsFalse(fakeBufferReader.TotalLength.HasValue);
            Assert.Throws(typeof(ArgumentNullException), () => fakeBufferReader.TryGetNextSlice(out Slice slice));
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
