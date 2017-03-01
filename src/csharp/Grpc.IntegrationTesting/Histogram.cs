#region Copyright notice and license

// Copyright 2015, gRPC authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
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
    /// Basic implementation of histogram based on grpc/support/histogram.h.
    /// </summary>
    public class Histogram
    {
        readonly object myLock = new object();
        readonly double multiplier;
        readonly double oneOnLogMultiplier;
        readonly double maxPossible;
        readonly uint[] buckets;

        int count;
        double sum;
        double sumOfSquares;
        double min;
        double max;

        public Histogram(double resolution, double maxPossible)
        {
            GrpcPreconditions.CheckArgument(resolution > 0);
            GrpcPreconditions.CheckArgument(maxPossible > 0);
            this.maxPossible = maxPossible;
            this.multiplier = 1.0 + resolution;
            this.oneOnLogMultiplier = 1.0 / Math.Log(1.0 + resolution);
            this.buckets = new uint[FindBucket(maxPossible) + 1];

            ResetUnsafe();
        }

        public void AddObservation(double value)
        {
            lock (myLock)
            {
                AddObservationUnsafe(value);    
            }
        }


        /// <summary>
        /// Gets snapshot of stats and reset 
        /// </summary>
        public HistogramData GetSnapshot(bool reset = false)
        {
            lock (myLock)
            {
                return GetSnapshotUnsafe(reset);    
            }
        }

        /// <summary>
        /// Finds bucket index to which given observation should go.
        /// </summary>
        private int FindBucket(double value)
        {
            value = Math.Max(value, 1.0);
            value = Math.Min(value, this.maxPossible);
            return (int)(Math.Log(value) * oneOnLogMultiplier);
        }

        private void AddObservationUnsafe(double value)
        {
            this.count++;
            this.sum += value;
            this.sumOfSquares += value * value;
            this.min = Math.Min(this.min, value);
            this.max = Math.Max(this.max, value);

            this.buckets[FindBucket(value)]++;
        }

        private HistogramData GetSnapshotUnsafe(bool reset)
        {
            var data = new HistogramData
            {
                Count = count,
                Sum = sum,
                SumOfSquares = sumOfSquares,
                MinSeen = min,
                MaxSeen = max,
                Bucket = { buckets }
            };

            if (reset)
            {
                ResetUnsafe();
            }

            return data;
        }

        private void ResetUnsafe()
        {
            this.count = 0;
            this.sum = 0;
            this.sumOfSquares = 0;
            this.min = double.PositiveInfinity;
            this.max = double.NegativeInfinity;
            for (int i = 0; i < this.buckets.Length; i++)
            {
                this.buckets[i] = 0;
            }
        }
    }
}
