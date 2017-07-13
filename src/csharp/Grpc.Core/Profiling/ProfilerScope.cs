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
    // Allows declaring Begin and End of a profiler scope with a using statement.
    // declared as struct for better performance.
    internal struct ProfilerScope : IDisposable
    {
        readonly IProfiler profiler;
        readonly string tag;

        public ProfilerScope(IProfiler profiler, string tag)
        {
            this.profiler = profiler;
            this.tag = tag;
            this.profiler.Begin(this.tag);
        }
            
        public void Dispose()
        {
            profiler.End(tag);
        }
    }
}
