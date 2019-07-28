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

using System.Runtime.InteropServices;

namespace Grpc.Core.Internal.Tests
{
    public class MarshalUtilsTest
    {
        [Test]
        public void PtrToStringASCII_NullNativeBufferThrows()
        {
            Assert.Throws(typeof(ArgumentNullException), () => MarshalUtils.PtrToStringASCII(IntPtr.Zero, 0));
        }

        [Test]
        public void PtrToStringASCII_ReturnsCachedEmptyString()
        {
            var emptyString1 = MarshalUtils.PtrToStringASCII(new IntPtr(1000), 0);
            var emptyString2 = MarshalUtils.PtrToStringASCII(new IntPtr(1000), 0);

            Assert.IsEmpty(emptyString1);
            Assert.IsEmpty(emptyString2);
            Assert.AreSame(emptyString1, emptyString2);
        }

        [Test]
        public unsafe void PtrToStringASCII_ValidASCII()
        {
            var data = new byte[] { 0x41, 0x42, 0x43 };
            fixed (byte* dataPtr = data)
            {
              var str = MarshalUtils.PtrToStringASCII((IntPtr) dataPtr, data.Length);
              Assert.AreEqual("ABC", str);
            }
        }

        [Test]
        public unsafe void PtrToStringASCII_InvalidASCII()
        {
            var data = new byte[] { 0x41, 0xff, 0x80, 0xc0 };
            fixed (byte* dataPtr = data)
            {
              var str = MarshalUtils.PtrToStringASCII((IntPtr) dataPtr, data.Length);
              Assert.AreEqual("A???", str);
            }
        }

        [Test]
        public unsafe void CStringToStringASCII_Null()
        {
            Assert.IsNull(MarshalUtils.CStringPtrToStringASCII(IntPtr.Zero));
        }

        [Test]
        public unsafe void CStringToStringASCII_ReturnsCachedEmptyString()
        {
            var data = new byte[] { 0x00 };
            fixed (byte* dataPtr = data)
            {
                var emptyString1 = MarshalUtils.CStringPtrToStringASCII((IntPtr) dataPtr);
                var emptyString2 = MarshalUtils.CStringPtrToStringASCII((IntPtr) dataPtr);

                Assert.IsEmpty(emptyString1);
                Assert.IsEmpty(emptyString2);
                Assert.AreSame(emptyString1, emptyString2);
            }
        }

        [Test]
        public unsafe void CStringToStringASCII_ValidASCII()
        {
            var data = new byte[] { 0x41, 0x42, 0x43, 0x00 };
            fixed (byte* dataPtr = data)
            {
                var str = MarshalUtils.CStringPtrToStringASCII((IntPtr) dataPtr);
                Assert.AreEqual("ABC", str);
            }
        }
    }
}
