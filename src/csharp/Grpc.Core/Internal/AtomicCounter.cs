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
using System.Threading;

namespace Grpc.Core.Internal
{
    internal class AtomicCounter
    {
        long counter = 0;

        public AtomicCounter(long initialCount = 0)
        {
            this.counter = initialCount;
        }

        public long Increment()
        {
            return Interlocked.Increment(ref counter);
        }

        public void IncrementIfNonzero(ref bool success)
        {
            long origValue = counter;
            while (true)
            {
                if (origValue == 0)
                {
                    success = false;
                    return;
                }
                long result = Interlocked.CompareExchange(ref counter, origValue + 1, origValue);
                if (result == origValue)
                {
                    success = true;
                    return;
                };
                origValue = result;
            }
        }

        public long Decrement()
        {
            return Interlocked.Decrement(ref counter);
        }

        public long Count
        {
            get
            {
                return Interlocked.Read(ref counter);
            }
        }
    }
}
