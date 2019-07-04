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

using System;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// this type is intended to be used **AS A FIELD**; it is a mutable struct; if you use
    /// it in any other way (in particular taking values into a local): all bets are off
    /// </summary>
    internal struct LazyTask<T> where T : class
    {
        private object valueOrTaskOrTCS;


        public Task<T> Task
        {
            get
            {
                while (true) // in reality this should iterate exactly once (no competition) or twice (competition)
                {
                    // this can happen if they only look *after* it has completed;
                    // we bypass the TCS
                    var asTask = valueOrTaskOrTCS as Task<T>;
                    if (asTask != null) return asTask;

                    // this can happen if they look *before* it has completed
                    var asTCS = valueOrTaskOrTCS as TaskCompletionSource<T>;
                    if (asTCS != null) return asTCS.Task;

                    // if it has completed, but they haven't looked yet, we won't
                    // have allocated a Task yet; fix that
                    var asTyped = valueOrTaskOrTCS as T;
                    if (asTyped != null)
                    {
                        var newTask = System.Threading.Tasks.Task.FromResult(asTyped);
                        if (Interlocked.CompareExchange(ref valueOrTaskOrTCS, newTask, asTyped) == asTyped)
                            return newTask;
                        // if we lose due to a race condition, try again (it should now be something usable)
                    }
                    else
                    {
                        // otherwise; null - nobody has looked yet and the data hasn't been assigned; allocate a TCS
                        var tcs = new TaskCompletionSource<T>();
                        var result = Interlocked.CompareExchange(ref valueOrTaskOrTCS, tcs, asTyped);
                        if (result == asTyped) return tcs.Task; // no competition; return the TCS

                        // we lost the race; there's a good chance that means that the value just got assigned
                        asTyped = result as T;
                        if (asTyped != null)
                        {
                            // it was indeed; that means we can complete out TCS
                            tcs.TrySetResult(asTyped);
                            return tcs.Task;
                        }
                        // we lost the race to something else, try again (it should now be something usable)
                    }
                }
            }
        }

        private static readonly Task<T> NullTypedTask = System.Threading.Tasks.Task.FromResult<T>(null);

        public bool TrySetResult(T value)
        {
            if (value == null)
            {   // swap in the static Task<T> with a null value if nothing already set
                if (Interlocked.CompareExchange(ref valueOrTaskOrTCS, NullTypedTask, null) == null)
                    return true;
            }

            var asTCS = valueOrTaskOrTCS as TaskCompletionSource<T>;
            if (asTCS != null)
            {   // update TCS to assign result
                return asTCS.TrySetResult(value);
            }

            // so it isn't a TCS and we have a value; we'll just store the value
            // directly; no need to spin up a Task<T> unless someone queries it
            var result = Interlocked.CompareExchange(ref valueOrTaskOrTCS, value, null);
            if (result != null)
            {
                // we lost a race; that *could* mean that someone peeked, and there's now
                // a TCS
                asTCS = result as TaskCompletionSource<T>;
                if (asTCS != null) return asTCS.TrySetResult(value);
            }
            // at this point, we've done all we can
            return false;
        }

        public bool TrySetException(Exception exception)
        {
            GrpcPreconditions.CheckNotNull(exception);
            if (valueOrTaskOrTCS == null)
            {
#if NET45
                var tcs = new TaskCompletionSource<T>();
                tcs.TrySetException(exception);
                var faultedTask = tcs.Task;
#else
                var faultedTask = System.Threading.Tasks.Task.FromException(exception);
#endif
                if (Interlocked.CompareExchange(ref valueOrTaskOrTCS, faultedTask, null) == null)
                    return true;
            }

            var asTCS = valueOrTaskOrTCS as TaskCompletionSource<T>;
            if (asTCS != null)
            {   // update TCS to assign result
                return asTCS.TrySetException(exception);
            }

            // not a TCS and not null? nothing we can do
            return false;
        }

        public T GetResult()
        {
            var typed = valueOrTaskOrTCS as T;
            if (typed != null) return typed;

            // Note that GetAwaiter().GetResult() doesn't wrap exceptions in AggregateException.
            return Task.GetAwaiter().GetResult();
        }
    }
}
