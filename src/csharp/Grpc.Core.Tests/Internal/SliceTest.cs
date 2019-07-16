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
        [TestCase(0)]
        [TestCase(1)]
        [TestCase(10)]
        [TestCase(100)]
        [TestCase(1000)]
        public void SliceFromNativePtr_Copy(int bufferLength)
        {
            var origBuffer = GetTestBuffer(bufferLength);
            var gcHandle = GCHandle.Alloc(origBuffer, GCHandleType.Pinned);
            try
            {
                var slice = new Slice(gcHandle.AddrOfPinnedObject(), origBuffer.Length);
                Assert.AreEqual(bufferLength, slice.Length);

                var newBuffer = new byte[bufferLength];
                slice.ToSpanUnsafe().CopyTo(newBuffer);
                CollectionAssert.AreEqual(origBuffer, newBuffer);
            }
            finally
            {
                gcHandle.Free();
            }
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
