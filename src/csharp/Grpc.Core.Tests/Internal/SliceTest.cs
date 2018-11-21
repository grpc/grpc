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

using System.Runtime.InteropServices;

namespace Grpc.Core.Internal.Tests
{
    public class SliceTest
    {
        [TestCase]
        public void InlineDataIsBigEnough()
        {
            Assert.IsTrue(Slice.InlineDataMaxLength >= Slice.NativeInlinedSize);
        }

        [TestCase(0)]
        [TestCase(1)]
        [TestCase(10)]
        [TestCase(100)]
        [TestCase(1000)]
        public void SliceFromNativePtr(int bufferLength)
        {
            var origBuffer = GetTestBuffer(bufferLength);
            var gcHandle = GCHandle.Alloc(origBuffer, GCHandleType.Pinned);
            try
            {
                var slice = new Slice(origBuffer.Length, gcHandle.AddrOfPinnedObject(), new Slice.InlineData());
                Assert.IsFalse(slice.IsInline);
                Assert.AreEqual(bufferLength, slice.Length);

                var newBuffer = new byte[bufferLength];
                slice.CopyTo(newBuffer);
                CollectionAssert.AreEqual(origBuffer, newBuffer);
            }
            finally
            {
                gcHandle.Free();
            }
        }

        [TestCase]
        public void CopyToBufferOfDifferentSize()
        {
            var slice = new Slice(10, IntPtr.Zero, new Slice.InlineData());
            var tooSmallBuffer = new byte[5];
            Assert.Throws(typeof(ArgumentException), () => slice.CopyTo(tooSmallBuffer));
            var tooLargeBuffer = new byte[20];
            Assert.DoesNotThrow(() => slice.CopyTo(tooLargeBuffer));
        }

        [TestCase(0)]
        [TestCase(1)]
        [TestCase(5)]
        [TestCase(10)]
        [TestCase(24)]
        public void SliceFromInlineData(int sliceLen)
        {
            var origBuffer = GetTestBuffer(sliceLen);
            var slice = Slice.CreateInlineFrom(origBuffer);
            Assert.IsTrue(slice.IsInline);
            Assert.AreEqual(origBuffer.Length, slice.Length);

            var newBuffer = new byte[origBuffer.Length];
            slice.CopyTo(newBuffer);
            CollectionAssert.AreEqual(origBuffer, newBuffer);
        }

        [TestCase]
        public void TooLongInlineData()
        {
            var origBuffer = new byte[Slice.InlineDataMaxLength + 1];
            Assert.Throws(typeof(ArgumentException), () => Slice.CreateInlineFrom(origBuffer));
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
