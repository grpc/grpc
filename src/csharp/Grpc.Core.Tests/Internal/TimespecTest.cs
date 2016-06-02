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
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Internal.Tests
{
    public class TimespecTest
    {
        [Test]
        public void Now_IsInUtc() 
        {
            Assert.AreEqual(DateTimeKind.Utc, Timespec.Now.ToDateTime().Kind);
        }

        [Test]
        public void Now_AgreesWithUtcNow()
        {
            var timespec = Timespec.Now;
            var utcNow = DateTime.UtcNow;

            TimeSpan difference = utcNow - timespec.ToDateTime();

            // This test is inherently a race - but the two timestamps
            // should really be way less that a minute apart.
            Assert.IsTrue(difference.TotalSeconds < 60);
        }

        [Test]
        public void InfFutureMatchesNativeValue()
        {
            Assert.AreEqual(Timespec.NativeInfFuture, Timespec.InfFuture);
        }

        [Test]
        public void InfPastMatchesNativeValue()
        {
            Assert.AreEqual(Timespec.NativeInfPast, Timespec.InfPast);
        }

        [Test]
        public void TimespecSizeIsNativeSize()
        {
            Assert.AreEqual(Timespec.NativeSize, Marshal.SizeOf(typeof(Timespec)));
        }

        [Test]
        public void ToDateTime()
        {
            Assert.AreEqual(new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc),
                new Timespec(0, 0).ToDateTime());

            Assert.AreEqual(new DateTime(1970, 1, 1, 0, 0, 10, DateTimeKind.Utc).AddTicks(50),
                new Timespec(10, 5000).ToDateTime());

            Assert.AreEqual(new DateTime(2015, 7, 21, 4, 21, 48, DateTimeKind.Utc),
                new Timespec(1437452508, 0).ToDateTime());

            // before epoch
            Assert.AreEqual(new DateTime(1969, 12, 31, 23, 59, 55, DateTimeKind.Utc).AddTicks(10),
                new Timespec(-5, 1000).ToDateTime());

            // infinity
            Assert.AreEqual(DateTime.MaxValue, Timespec.InfFuture.ToDateTime());
            Assert.AreEqual(DateTime.MinValue, Timespec.InfPast.ToDateTime());

            // nanos are rounded to ticks are rounded up
            Assert.AreEqual(new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc).AddTicks(1),
                new Timespec(0, 99).ToDateTime());

            // Illegal inputs
            Assert.Throws(typeof(InvalidOperationException),
                () => new Timespec(0, -2).ToDateTime());
            Assert.Throws(typeof(InvalidOperationException),
                () => new Timespec(0, 1000 * 1000 * 1000).ToDateTime());
            Assert.Throws(typeof(InvalidOperationException),
                () => new Timespec(0, 0, ClockType.Monotonic).ToDateTime());
        }

        [Test]
        public void ToDateTime_ReturnsUtc()
        {
            Assert.AreEqual(DateTimeKind.Utc, new Timespec(1437452508, 0).ToDateTime().Kind);
            Assert.AreNotEqual(DateTimeKind.Unspecified, new Timespec(1437452508, 0).ToDateTime().Kind);
        }

        [Test]
        public void ToDateTime_Overflow()
        {     
            var timespec = new Timespec(long.MaxValue - 100, 0);
            Assert.AreNotEqual(Timespec.InfFuture, timespec);
            Assert.AreEqual(DateTime.MaxValue, timespec.ToDateTime());

            Assert.AreEqual(DateTime.MinValue, new Timespec(long.MinValue + 100, 0).ToDateTime());
        }

        [Test]
        public void ToDateTime_OutOfDateTimeRange()
        {
            // DateTime range goes up to year 9999, 20000 years from now should
            // be out of range.
            long seconds = 20000L * 365L * 24L * 3600L; 

            var timespec = new Timespec(seconds, 0);
            Assert.AreNotEqual(Timespec.InfFuture, timespec);
            Assert.AreEqual(DateTime.MaxValue, timespec.ToDateTime());

            Assert.AreEqual(DateTime.MinValue, new Timespec(-seconds, 0).ToDateTime());
        }

        [Test]
        public void FromDateTime()
        {
            Assert.AreEqual(new Timespec(0, 0),
                Timespec.FromDateTime(new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc)));

            Assert.AreEqual(new Timespec(10, 5000),
                Timespec.FromDateTime(new DateTime(1970, 1, 1, 0, 0, 10, DateTimeKind.Utc).AddTicks(50)));

            Assert.AreEqual(new Timespec(1437452508, 0),
                Timespec.FromDateTime(new DateTime(2015, 7, 21, 4, 21, 48, DateTimeKind.Utc)));

            // before epoch
            Assert.AreEqual(new Timespec(-5, 1000),
                Timespec.FromDateTime(new DateTime(1969, 12, 31, 23, 59, 55, DateTimeKind.Utc).AddTicks(10)));

            // infinity
            Assert.AreEqual(Timespec.InfFuture, Timespec.FromDateTime(DateTime.MaxValue));
            Assert.AreEqual(Timespec.InfPast, Timespec.FromDateTime(DateTime.MinValue));

            // illegal inputs
            Assert.Throws(typeof(ArgumentException),
                () => Timespec.FromDateTime(new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Unspecified)));
        }
            
        [Test]
        [Category("Performance")]
        [Ignore("Prevent running on Jenkins")]
        public void NowBenchmark() 
        {
            // approx Timespec.Now latency <33ns
            BenchmarkUtil.RunBenchmark(10000000, 1000000000, () => { var now = Timespec.Now; });
        }
            
        [Test]
        [Category("Performance")]
        [Ignore("Prevent running on Jenkins")]
        public void PreciseNowBenchmark()
        {
            // approx Timespec.PreciseNow latency <18ns (when compiled with GRPC_TIMERS_RDTSC)
            BenchmarkUtil.RunBenchmark(10000000, 1000000000, () => { var now = Timespec.PreciseNow; });
        }
    }
}
