#region Copyright notice and license

// Copyright 2016, Google Inc.
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
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;

namespace Grpc.IntegrationTesting
{
    public interface IInterarrivalTimer
    {
        void WaitForNext();

        Task WaitForNextAsync();
    }

    /// <summary>
    /// Interarrival timer that doesn't wait at all.
    /// </summary>
    public class ClosedLoopInterarrivalTimer : IInterarrivalTimer
    {
        public ClosedLoopInterarrivalTimer()
        {
        }

        public void WaitForNext()
        {
            // NOP
        }

        public Task WaitForNextAsync()
        {
            return Task.FromResult<object>(null);
        }
    }

    /// <summary>
    /// Interarrival timer that generates Poisson process load.
    /// </summary>
    public class PoissonInterarrivalTimer : IInterarrivalTimer
    {
        readonly ExponentialDistribution exponentialDistribution;
        DateTime? lastEventTime;

        public PoissonInterarrivalTimer(double offeredLoad)
        {
            this.exponentialDistribution = new ExponentialDistribution(new Random(), offeredLoad);
            this.lastEventTime = DateTime.UtcNow;
        }

        public void WaitForNext()
        {
            var waitDuration = GetNextWaitDuration();
            int millisTimeout = (int) Math.Round(waitDuration.TotalMilliseconds);
            if (millisTimeout > 0)
            {
                // TODO(jtattermusch): probably only works well for a relatively low interarrival rate
                Thread.Sleep(millisTimeout);
            }
        }

        public async Task WaitForNextAsync()
        {
            var waitDuration = GetNextWaitDuration();
            int millisTimeout = (int) Math.Round(waitDuration.TotalMilliseconds);
            if (millisTimeout > 0)
            {
                // TODO(jtattermusch): probably only works well for a relatively low interarrival rate
                await Task.Delay(millisTimeout);
            }
        }

        private TimeSpan GetNextWaitDuration()
        {
            if (!lastEventTime.HasValue)
            {
                this.lastEventTime = DateTime.Now;
            }

            var origLastEventTime = this.lastEventTime.Value;
            this.lastEventTime = origLastEventTime + TimeSpan.FromSeconds(exponentialDistribution.Next());
            return this.lastEventTime.Value - origLastEventTime;
        }

        /// <summary>
        /// Exp generator.
        /// </summary>
        private class ExponentialDistribution
        {
            readonly Random random;
            readonly double lambda;
            readonly double lambdaReciprocal;

            public ExponentialDistribution(Random random, double lambda)
            {
                this.random = random;
                this.lambda = lambda;
                this.lambdaReciprocal = 1.0 / lambda;
            }

            public double Next()
            {
                double uniform = random.NextDouble();
                // Use 1.0-uni above to avoid NaN if uni is 0
                return lambdaReciprocal * (-Math.Log(1.0 - uniform));
            }
        }
    }
}
