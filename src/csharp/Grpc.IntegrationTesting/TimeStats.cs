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
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Utils;
using NUnit.Framework;
using Grpc.Testing;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Snapshottable time statistics.
    /// </summary>
    public class TimeStats
    {
        readonly object myLock = new object();
        DateTime lastWallClock;
        TimeSpan lastUserTime;
        TimeSpan lastPrivilegedTime;

        public TimeStats()
        {
            lastWallClock = DateTime.UtcNow;
            lastUserTime = Process.GetCurrentProcess().UserProcessorTime;
            lastPrivilegedTime = Process.GetCurrentProcess().PrivilegedProcessorTime;
        }

        public Snapshot GetSnapshot(bool reset)
        {
            lock (myLock)
            {
                var wallClock = DateTime.UtcNow;
                var userTime = Process.GetCurrentProcess().UserProcessorTime;
                var privilegedTime = Process.GetCurrentProcess().PrivilegedProcessorTime;
                var snapshot = new Snapshot(wallClock - lastWallClock, userTime - lastUserTime, privilegedTime - lastPrivilegedTime);

                if (reset)
                {
                    lastWallClock = wallClock;
                    lastUserTime = userTime;
                    lastPrivilegedTime = privilegedTime;
                }
                return snapshot;
            }
        }

        public class Snapshot
        {
            public TimeSpan WallClockTime { get; }
            public TimeSpan UserProcessorTime { get; }
            public TimeSpan PrivilegedProcessorTime { get; }

            public Snapshot(TimeSpan wallClockTime, TimeSpan userProcessorTime, TimeSpan privilegedProcessorTime)
            {
                this.WallClockTime = wallClockTime;
                this.UserProcessorTime = userProcessorTime;
                this.PrivilegedProcessorTime = privilegedProcessorTime;
            }

            public override string ToString()
            {
                return string.Format("[TimeStats.Snapshot: wallClock {0}, userProcessor {1}, privilegedProcessor {2}]", WallClockTime, UserProcessorTime, PrivilegedProcessorTime);
            }
        }
    }
}
