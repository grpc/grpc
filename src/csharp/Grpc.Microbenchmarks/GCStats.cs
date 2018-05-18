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
using Grpc.Core;
using Grpc.Core.Internal;

namespace Grpc.Microbenchmarks
{
    internal class GCStats
    {
        readonly object myLock = new object();
        GCStatsSnapshot lastSnapshot;

        public GCStats()
        {
            lastSnapshot = new GCStatsSnapshot(GC.CollectionCount(0), GC.CollectionCount(1), GC.CollectionCount(2));
        }

        public GCStatsSnapshot GetSnapshot(bool reset = false)
        {
            lock (myLock)
            {
                var newSnapshot = new GCStatsSnapshot(GC.CollectionCount(0) - lastSnapshot.Gen0,
                    GC.CollectionCount(1) - lastSnapshot.Gen1,
                    GC.CollectionCount(2) - lastSnapshot.Gen2);
                if (reset)
                {
                    lastSnapshot = newSnapshot;
                }
                return newSnapshot;
            }
        }
    }

    public class GCStatsSnapshot
    {
        public GCStatsSnapshot(int gen0, int gen1, int gen2)
        {
            this.Gen0 = gen0;
            this.Gen1 = gen1;
            this.Gen2 = gen2;
        }

        public int Gen0 { get; }
        public int Gen1 { get; }
        public int Gen2 { get; }

        public override string ToString()
        {
            return string.Format("[GCCollectionCount: gen0 {0}, gen1 {1}, gen2 {2}]", Gen0, Gen1, Gen2);
        }
    }
}
