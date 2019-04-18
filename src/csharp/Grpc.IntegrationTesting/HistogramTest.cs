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
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class HistogramTest
    {
        [Test]
        public void Simple()
        {
            var hist = new Histogram(0.01, 60e9);
            hist.AddObservation(10000);
            hist.AddObservation(10000);
            hist.AddObservation(11000);
            hist.AddObservation(11000);

            var data = hist.GetSnapshot();

            Assert.AreEqual(4, data.Count);
            Assert.AreEqual(42000.0, data.Sum, 1e-6);
            Assert.AreEqual(10000, data.MinSeen);
            Assert.AreEqual(11000, data.MaxSeen);
            Assert.AreEqual(2.0*10000*10000 + 2.0*11000*11000, data.SumOfSquares, 1e-6);

            // 1.01^925 < 10000 < 1.01^926
            Assert.AreEqual(2, data.Bucket[925]);
            Assert.AreEqual(2, data.Bucket[935]);
        }

        [Test]
        public void ExtremeObservations()
        {
            var hist = new Histogram(0.01, 60e9);
            hist.AddObservation(-0.5);  // should be in the first bucket
            hist.AddObservation(1e12);  // should be in the last bucket

            var data = hist.GetSnapshot();
            Assert.AreEqual(1, data.Bucket[0]);
            Assert.AreEqual(1, data.Bucket[data.Bucket.Count - 1]);
        }

        [Test]
        public void MergeSnapshots()
        {
            var data = new HistogramData();

            var hist1 = new Histogram(0.01, 60e9);
            hist1.AddObservation(-0.5);  // should be in the first bucket
            hist1.AddObservation(1e12);  // should be in the last bucket
            hist1.GetSnapshot(data, false);

            var hist2 = new Histogram(0.01, 60e9);
            hist2.AddObservation(10000);
            hist2.AddObservation(11000);
            hist2.GetSnapshot(data, false);

            Assert.AreEqual(4, data.Count);
            Assert.AreEqual(-0.5, data.MinSeen);
            Assert.AreEqual(1e12, data.MaxSeen);
            Assert.AreEqual(1, data.Bucket[0]);
            Assert.AreEqual(1, data.Bucket[925]);
            Assert.AreEqual(1, data.Bucket[935]);
            Assert.AreEqual(1, data.Bucket[data.Bucket.Count - 1]);
        }

        [Test]
        public void Reset()
        {
            var hist = new Histogram(0.01, 60e9);
            hist.AddObservation(10000);
            hist.AddObservation(11000);

            var data = hist.GetSnapshot(true);  // snapshot contains data before reset
            Assert.AreEqual(2, data.Count);
            Assert.AreEqual(10000, data.MinSeen);
            Assert.AreEqual(11000, data.MaxSeen);

            data = hist.GetSnapshot();  // snapshot contains state after reset
            Assert.AreEqual(0, data.Count);
            Assert.AreEqual(double.PositiveInfinity, data.MinSeen);
            Assert.AreEqual(double.NegativeInfinity, data.MaxSeen);
            Assert.AreEqual(0, data.Sum);
            Assert.AreEqual(0, data.SumOfSquares);
            CollectionAssert.AreEqual(new uint[data.Bucket.Count], data.Bucket); 
        }
    }
}
