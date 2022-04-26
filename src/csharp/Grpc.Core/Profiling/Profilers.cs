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
using System.IO;
using System.Threading;
using Grpc.Core.Internal;

namespace Grpc.Core.Profiling
{
    internal static class Profilers
    {
        static readonly NopProfiler DefaultProfiler = new NopProfiler();
        static readonly ThreadLocal<IProfiler> profilers = new ThreadLocal<IProfiler>();

        public static IProfiler ForCurrentThread()
        {
            return profilers.Value ?? DefaultProfiler;
        }

        public static void SetForCurrentThread(IProfiler profiler)
        {
            profilers.Value = profiler;
        }

        public static ProfilerScope NewScope(this IProfiler profiler, string tag)
        {
            return new ProfilerScope(profiler, tag);
        }
    }

    internal class NopProfiler : IProfiler
    {
        public void Begin(string tag)
        {
        }

        public void End(string tag)
        {
        }

        public void Mark(string tag)
        {
        }
    }

    // Profiler using Timespec.PreciseNow
    internal class BasicProfiler : IProfiler
    {
        ProfilerEntry[] entries;
        int count;

        public BasicProfiler() : this(20*1024*1024)
        {
        }

        public BasicProfiler(int capacity)
        {
            this.entries = new ProfilerEntry[capacity];
        }

        public void Begin(string tag)
        {
            AddEntry(new ProfilerEntry(Timespec.PreciseNow, ProfilerEntry.Type.BEGIN, tag));
        }

        public void End(string tag)
        {
            AddEntry(new ProfilerEntry(Timespec.PreciseNow, ProfilerEntry.Type.END, tag));
        }

        public void Mark(string tag)
        {
            AddEntry(new ProfilerEntry(Timespec.PreciseNow, ProfilerEntry.Type.MARK, tag));
        }

        public void Reset()
        {
            count = 0;
        }

        public void Dump(string filepath)
        {
            using (var stream = File.CreateText(filepath))
            {
                Dump(stream);
            }
        }

        public void Dump(TextWriter stream)
        {
            for (int i = 0; i < count; i++)
            {
                var entry = entries[i];
                stream.WriteLine(entry.ToString());
            }
        }

        // NOT THREADSAFE!
        void AddEntry(ProfilerEntry entry) {
            entries[count++] = entry;
        }
    }
}
