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

namespace Grpc.Core.Internal
{
    /// <summary>
    /// gpr_timespec from grpc/support/time.h
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct Timespec
    {
        const int NanosPerSecond = 1000 * 1000 * 1000;
        const int NanosPerTick = 100;

        [DllImport("grpc_csharp_ext.dll")]
        static extern Timespec gprsharp_now();

        [DllImport("grpc_csharp_ext.dll")]
        static extern Timespec gprsharp_inf_future();

        [DllImport("grpc_csharp_ext.dll")]
        static extern int gprsharp_sizeof_timespec();

        // NOTE: on linux 64bit  sizeof(gpr_timespec) = 16, on windows 32bit sizeof(gpr_timespec) = 8
        // so IntPtr seems to have the right size to work on both.
        public System.IntPtr tv_sec;
        public int tv_nsec;
        public GPRClockType clock_type;

        /// <summary>
        /// Timespec a long time in the future.
        /// </summary>
        public static Timespec InfFuture
        {
            get
            {
                return gprsharp_inf_future();
            }
        }

        public static Timespec Now
        {
            get
            {
                return gprsharp_now();
            }
        }

        internal static int NativeSize
        {
            get
            {
                return gprsharp_sizeof_timespec();
            }
        }

        /// <summary>
        /// Creates a GPR deadline from current instant and given timeout.
        /// </summary>
        /// <returns>The from timeout.</returns>
        public static Timespec DeadlineFromTimeout(TimeSpan timeout)
        {
            if (timeout == Timeout.InfiniteTimeSpan)
            {
                return Timespec.InfFuture;
            }
            return Timespec.Now.Add(timeout);
        }

        public Timespec Add(TimeSpan timeSpan)
        {
            long nanos = (long)tv_nsec + (timeSpan.Ticks % TimeSpan.TicksPerSecond) * NanosPerTick;
            long overflow_sec = (nanos > NanosPerSecond) ? 1 : 0;

            Timespec result;
            result.tv_nsec = (int)(nanos % NanosPerSecond);
            result.tv_sec = new IntPtr(tv_sec.ToInt64() + (timeSpan.Ticks / TimeSpan.TicksPerSecond) + overflow_sec);
            result.clock_type = GPRClockType.Realtime;
            return result;
        }
    }
}
