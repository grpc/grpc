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
