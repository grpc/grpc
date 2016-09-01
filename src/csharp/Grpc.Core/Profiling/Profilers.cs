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

        public BasicProfiler() : this(1024*1024)
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
