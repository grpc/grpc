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
using System.Threading;

using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// gpr_timespec from grpc/support/time.h
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct Timespec
    {
        const long NanosPerSecond = 1000 * 1000 * 1000;
        const long NanosPerTick = 100;
        const long TicksPerSecond = NanosPerSecond / NanosPerTick;

        static readonly NativeMethods Native = NativeMethods.Get();
        static readonly DateTime UnixEpoch = new DateTime(1970, 1, 1, 0, 0, 0, 0, DateTimeKind.Utc);

        public Timespec(long tv_sec, int tv_nsec) : this(tv_sec, tv_nsec, ClockType.Realtime)
        {
        }

        public Timespec(long tv_sec, int tv_nsec, ClockType clock_type)
        {
            this.tv_sec = tv_sec;
            this.tv_nsec = tv_nsec;
            this.clock_type = clock_type;
        }

        private long tv_sec;
        private int tv_nsec;
        private ClockType clock_type;

        /// <summary>
        /// Timespec a long time in the future.
        /// </summary>
        public static Timespec InfFuture
        {
            get
            {
                return new Timespec(long.MaxValue, 0, ClockType.Realtime);
            }
        }

        /// <summary>
        /// Timespec a long time in the past.
        /// </summary>
        public static Timespec InfPast
        {
            get
            {
                return new Timespec(long.MinValue, 0, ClockType.Realtime);
            }
        }

        /// <summary>
        /// Return Timespec representing the current time.
        /// </summary>
        public static Timespec Now
        {
            get
            {
                return Native.gprsharp_now(ClockType.Realtime);
            }
        }

        /// <summary>
        /// Seconds since unix epoch.
        /// </summary>
        public long TimevalSeconds
        {
            get
            {
                return tv_sec;
            }
        }

        /// <summary>
        /// The nanoseconds part of timeval.
        /// </summary>
        public int TimevalNanos
        {
            get
            {
                return tv_nsec;
            }
        }

        /// <summary>
        /// Converts the timespec to desired clock type.
        /// </summary>
        public Timespec ToClockType(ClockType targetClock)
        {
            return Native.gprsharp_convert_clock_type(this, targetClock);
        }
            
        /// <summary>
        /// Converts Timespec to DateTime.
        /// Timespec needs to be of type GPRClockType.Realtime and needs to represent a legal value.
        /// DateTime has lower resolution (100ns), so rounding can occurs.
        /// Value are always rounded up to the nearest DateTime value in the future.
        /// 
        /// For Timespec.InfFuture or if timespec is after the largest representable DateTime, DateTime.MaxValue is returned.
        /// For Timespec.InfPast or if timespec is before the lowest representable DateTime, DateTime.MinValue is returned.
        /// 
        /// Unless DateTime.MaxValue or DateTime.MinValue is returned, the resulting DateTime is always in UTC
        /// (DateTimeKind.Utc)
        /// </summary>
        public DateTime ToDateTime()
        {
            GrpcPreconditions.CheckState(tv_nsec >= 0 && tv_nsec < NanosPerSecond);
            GrpcPreconditions.CheckState(clock_type == ClockType.Realtime);

            // fast path for InfFuture
            if (this.Equals(InfFuture))
            {
                return DateTime.MaxValue;
            }

            // fast path for InfPast
            if (this.Equals(InfPast))
            {
                return DateTime.MinValue;
            }

            try
            {
                // convert nanos to ticks, round up to the nearest tick
                long ticksFromNanos = tv_nsec / NanosPerTick + ((tv_nsec % NanosPerTick != 0) ? 1 : 0);
                long ticksTotal = checked(tv_sec * TicksPerSecond + ticksFromNanos);
                return UnixEpoch.AddTicks(ticksTotal);
            }
            catch (OverflowException)
            {
                // ticks out of long range
                return tv_sec > 0 ? DateTime.MaxValue : DateTime.MinValue;
            }
            catch (ArgumentOutOfRangeException)
            {
                // resulting date time would be larger than MaxValue
                return tv_sec > 0 ? DateTime.MaxValue : DateTime.MinValue;
            }
        }
            
        /// <summary>
        /// Creates DateTime to Timespec.
        /// DateTime has to be in UTC (DateTimeKind.Utc) unless it's DateTime.MaxValue or DateTime.MinValue.
        /// For DateTime.MaxValue of date time after the largest representable Timespec, Timespec.InfFuture is returned.
        /// For DateTime.MinValue of date time before the lowest representable Timespec, Timespec.InfPast is returned.
        /// </summary>
        /// <returns>The date time.</returns>
        /// <param name="dateTime">Date time.</param>
        public static Timespec FromDateTime(DateTime dateTime)
        {
            if (dateTime == DateTime.MaxValue)
            {
                return Timespec.InfFuture;
            }

            if (dateTime == DateTime.MinValue)
            {
                return Timespec.InfPast;
            }

            GrpcPreconditions.CheckArgument(dateTime.Kind == DateTimeKind.Utc, "dateTime needs of kind DateTimeKind.Utc or be equal to DateTime.MaxValue or DateTime.MinValue.");

            try
            {
                TimeSpan timeSpan = dateTime - UnixEpoch;
                long ticks = timeSpan.Ticks;

                long seconds = ticks / TicksPerSecond;  
                int nanos = (int)((ticks % TicksPerSecond) * NanosPerTick);
                if (nanos < 0) 
                {
                    // correct the result based on C# modulo semantics for negative dividend
                    seconds--;
                    nanos += (int)NanosPerSecond;
                }
                return new Timespec(seconds, nanos);
            }
            catch (ArgumentOutOfRangeException)
            {
                return dateTime > UnixEpoch ? Timespec.InfFuture : Timespec.InfPast;
            }
        }

        /// <summary>
        /// Gets current timestamp using <c>GPRClockType.Precise</c>.
        /// Only available internally because core needs to be compiled with 
        /// GRPC_TIMERS_RDTSC support for this to use RDTSC.
        /// </summary>
        internal static Timespec PreciseNow
        {
            get
            {
                return Native.gprsharp_now(ClockType.Precise);
            }
        }

        // for tests only
        internal static int NativeSize
        {
            get
            {
                return Native.gprsharp_sizeof_timespec();
            }
        }

        // for tests only
        internal static Timespec NativeInfFuture
        {
            get
            {
                return Native.gprsharp_inf_future(ClockType.Realtime);
            }
        }

        // for tests only
        public static Timespec NativeInfPast
        {
            get
            {
                return Native.gprsharp_inf_past(ClockType.Realtime);
            }
        }
    }
}
