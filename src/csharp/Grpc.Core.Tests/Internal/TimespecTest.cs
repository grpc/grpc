#region Copyright notice and license

// Copyright 2015 gRPC authors.
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
            #pragma warning disable 0618
            // We need to use the obsolete non-generic version of Marshal.SizeOf because the generic version is not available in net45
            Assert.AreEqual(Timespec.NativeSize, Marshal.SizeOf(typeof(Timespec)));
            #pragma warning restore 0618
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
