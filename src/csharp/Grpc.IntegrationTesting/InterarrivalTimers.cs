#region Copyright notice and license

// Copyright 2016 gRPC authors.
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
            return TaskUtils.CompletedTask;
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
