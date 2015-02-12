using System;
using NUnit.Framework;
using System.Runtime.InteropServices;
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
            var t = new Timespec { tv_sec = new IntPtr(12345), tv_nsec = new IntPtr(123456789) };
            var result = t.Add(TimeSpan.FromTicks(TimeSpan.TicksPerSecond * 10));
            Assert.AreEqual(result.tv_sec, new IntPtr(12355));
            Assert.AreEqual(result.tv_nsec, new IntPtr(123456789));
        }

        [Test]
        public void Add_Nanos()
        {
            var t = new Timespec { tv_sec = new IntPtr(12345), tv_nsec = new IntPtr(123456789) };
            var result = t.Add(TimeSpan.FromTicks(10));
            Assert.AreEqual(result.tv_sec, new IntPtr(12345));
            Assert.AreEqual(result.tv_nsec, new IntPtr(123456789 + 1000));
        }

        [Test]
        public void Add_NanosOverflow()
        {
            var t = new Timespec { tv_sec = new IntPtr(12345), tv_nsec = new IntPtr(999999999) };
            var result = t.Add(TimeSpan.FromTicks(TimeSpan.TicksPerSecond * 10 + 10));
            Assert.AreEqual(result.tv_sec, new IntPtr(12356));
            Assert.AreEqual(result.tv_nsec, new IntPtr(999));
        }
    }
}

