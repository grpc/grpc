using System;
using NUnit.Framework;
using Google.GRPC.Core.Internal;

namespace Google.GRPC.Core.Internal.Tests
{
    public class TimespecTest
    {
        [Test]
        public void Now()
        {
            var timespec = Timespec.Now;
        }

        [Test]
        public void Add()
        {
            var t = new Timespec { tv_sec = 12345, tv_nsec = 123456789 };
            var result = t.Add(TimeSpan.FromTicks(TimeSpan.TicksPerSecond * 10));
            Assert.AreEqual(result.tv_sec, 12355);
            Assert.AreEqual(result.tv_nsec, 123456789);
        }

        [Test]
        public void Add_Nanos()
        {
            var t = new Timespec { tv_sec = 12345, tv_nsec = 123456789 };
            var result = t.Add(TimeSpan.FromTicks(10));
            Assert.AreEqual(result.tv_sec, 12345);
            Assert.AreEqual(result.tv_nsec, 123456789 + 1000);
        }

        [Test]
        public void Add_NanosOverflow()
        {
            var t = new Timespec { tv_sec = 12345, tv_nsec = 999999999 };
            var result = t.Add(TimeSpan.FromTicks(TimeSpan.TicksPerSecond * 10 + 10));
            Assert.AreEqual(result.tv_sec, 12356);
            Assert.AreEqual(result.tv_nsec, 999);
        }
    }
}

