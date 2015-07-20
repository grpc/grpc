#region Copyright notice and license

// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#endregion

using System;
using System.Runtime.InteropServices;
using Grpc.Core.Internal;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class TimespecTest
    {
        [Test]
        public void Now()
        {
            var timespec = Timespec.Now;
        }

        [Test]
        public void InfFuture()
        {
            var timespec = Timespec.InfFuture;
        }

        [Test]
        public void TimespecSizeIsNativeSize()
        {
            Assert.AreEqual(Timespec.NativeSize, Marshal.SizeOf(typeof(Timespec)));
        }

        [Test]
        public void Add()
        {
            var t = new Timespec { tv_sec = new IntPtr(12345), tv_nsec = 123456789 };
            var result = t.Add(TimeSpan.FromTicks(TimeSpan.TicksPerSecond * 10));
            Assert.AreEqual(result.tv_sec, new IntPtr(12355));
            Assert.AreEqual(result.tv_nsec, 123456789);
        }

        [Test]
        public void Add_Nanos()
        {
            var t = new Timespec { tv_sec = new IntPtr(12345), tv_nsec = 123456789 };
            var result = t.Add(TimeSpan.FromTicks(10));
            Assert.AreEqual(result.tv_sec, new IntPtr(12345));
            Assert.AreEqual(result.tv_nsec, 123456789 + 1000);
        }

        [Test]
        public void Add_NanosOverflow()
        {
            var t = new Timespec { tv_sec = new IntPtr(12345), tv_nsec = 999999999 };
            var result = t.Add(TimeSpan.FromTicks(TimeSpan.TicksPerSecond * 10 + 10));
            Assert.AreEqual(result.tv_sec, new IntPtr(12356));
            Assert.AreEqual(result.tv_nsec, 999);
        }
    }
}
