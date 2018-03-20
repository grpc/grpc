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
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal delegate void BatchCompletionDelegate(bool success, BatchContextSafeHandle ctx, object state);

    internal delegate void RequestCallCompletionDelegate(bool success, RequestCallContextSafeHandle ctx);

    internal class CompletionRegistry
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<CompletionRegistry>();

        readonly GrpcEnvironment environment;
        readonly Func<BatchContextSafeHandle> batchContextFactory;
        readonly Func<RequestCallContextSafeHandle> requestCallContextFactory;
        readonly Dictionary<IntPtr, IOpCompletionCallback> dict = new Dictionary<IntPtr, IOpCompletionCallback>(new IntPtrComparer());
        SpinLock spinLock = new SpinLock(Debugger.IsAttached);
        IntPtr lastRegisteredKey;  // only for testing

        public CompletionRegistry(GrpcEnvironment environment, Func<BatchContextSafeHandle> batchContextFactory, Func<RequestCallContextSafeHandle> requestCallContextFactory)
        {
            this.environment = GrpcPreconditions.CheckNotNull(environment);
            this.batchContextFactory = GrpcPreconditions.CheckNotNull(batchContextFactory);
            this.requestCallContextFactory = GrpcPreconditions.CheckNotNull(requestCallContextFactory);
        }

        public void Register(IntPtr key, IOpCompletionCallback callback)
        {
            environment.DebugStats.PendingBatchCompletions.Increment();

            bool lockTaken = false;
            try
            {
                spinLock.Enter(ref lockTaken);

                dict.Add(key, callback);
                this.lastRegisteredKey = key;
            }
            finally
            {
                if (lockTaken) spinLock.Exit();
            }
        }

        public BatchContextSafeHandle RegisterBatchCompletion(BatchCompletionDelegate callback, object state)
        {
            var ctx = batchContextFactory();
            ctx.SetCompletionCallback(callback, state);
            Register(ctx.Handle, ctx);
            return ctx;
        }

        public RequestCallContextSafeHandle RegisterRequestCallCompletion(RequestCallCompletionDelegate callback)
        {
            var ctx = requestCallContextFactory();
            ctx.CompletionCallback = callback;
            Register(ctx.Handle, ctx);
            return ctx;
        }

        public IOpCompletionCallback Extract(IntPtr key)
        {
            IOpCompletionCallback value = null;
            bool lockTaken = false;
            try
            {
                spinLock.Enter(ref lockTaken);

                value = dict[key];
                dict.Remove(key);
            }
            finally
            {
                if (lockTaken) spinLock.Exit();
            }
            environment.DebugStats.PendingBatchCompletions.Decrement();
            return value;
        }

        /// <summary>
        /// For testing purposes only. NOT threadsafe.
        /// </summary>
        public IntPtr LastRegisteredKey
        {
            get { return this.lastRegisteredKey; }
        }

        /// <summary>
        /// IntPtr doesn't implement <c>IEquatable{IntPtr}</c> so we need to use custom comparer to avoid boxing.
        /// </summary>
        private class IntPtrComparer : IEqualityComparer<IntPtr>
        {
            public bool Equals(IntPtr x, IntPtr y)
            {
                return x == y;
            }

            public int GetHashCode(IntPtr obj)
            {
                return obj.GetHashCode();
            }
        }
    }
}
