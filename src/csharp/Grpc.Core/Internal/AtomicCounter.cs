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

using System.Runtime.CompilerServices;
using System.Threading;

namespace Grpc.Core.Internal
{
    internal static class AtomicCounter
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static long Create(long initialCount = 0)
        {
            return initialCount;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static long Increment(ref long counter)
        {
            return Interlocked.Increment(ref counter);
        }

        public static bool IncrementIfNonzero(ref long counter)
        {
            long origValue = Volatile.Read(ref counter);
            while (true)
            {
                if (origValue == 0)
                {
                    return false;
                }
                long result = Interlocked.CompareExchange(ref counter, origValue + 1, origValue);
                if (result == origValue)
                {
                    return true;
                }
                origValue = result;
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static long Decrement(ref long counter)
        {
            return Interlocked.Decrement(ref counter);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public static long Count(ref long counter)
        {
            return Interlocked.Read(ref counter);
        }
    }
}
