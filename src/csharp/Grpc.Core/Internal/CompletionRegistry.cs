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
using System.Runtime.InteropServices;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal delegate void OpCompletionDelegate(bool success);

    internal delegate void BatchCompletionDelegate(bool success, BatchContextSafeHandle ctx);

    internal delegate void RequestCallCompletionDelegate(bool success, RequestCallContextSafeHandle ctx);

    internal class CompletionRegistry
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<CompletionRegistry>();

        readonly GrpcEnvironment environment;
        readonly ConcurrentDictionary<IntPtr, OpCompletionDelegate> dict = new ConcurrentDictionary<IntPtr, OpCompletionDelegate>(new IntPtrComparer());
        IntPtr lastRegisteredKey;  // only for testing

        public CompletionRegistry(GrpcEnvironment environment)
        {
            this.environment = environment;
        }

        public void Register(IntPtr key, OpCompletionDelegate callback)
        {
            environment.DebugStats.PendingBatchCompletions.Increment();
            GrpcPreconditions.CheckState(dict.TryAdd(key, callback));
            this.lastRegisteredKey = key;
        }

        public void RegisterBatchCompletion(BatchContextSafeHandle ctx, BatchCompletionDelegate callback)
        {
            OpCompletionDelegate opCallback = ((success) => HandleBatchCompletion(success, ctx, callback));
            Register(ctx.Handle, opCallback);
        }

        public void RegisterRequestCallCompletion(RequestCallContextSafeHandle ctx, RequestCallCompletionDelegate callback)
        {
            OpCompletionDelegate opCallback = ((success) => HandleRequestCallCompletion(success, ctx, callback));
            Register(ctx.Handle, opCallback);
        }

        public OpCompletionDelegate Extract(IntPtr key)
        {
            OpCompletionDelegate value;
            GrpcPreconditions.CheckState(dict.TryRemove(key, out value));
            environment.DebugStats.PendingBatchCompletions.Decrement();
            return value;
        }

        /// <summary>
        /// For testing purposes only.
        /// </summary>
        public IntPtr LastRegisteredKey
        {
            get { return this.lastRegisteredKey; }
        }

        private static void HandleBatchCompletion(bool success, BatchContextSafeHandle ctx, BatchCompletionDelegate callback)
        {
            try
            {
                callback(success, ctx);
            }
            catch (Exception e)
            {
                Logger.Error(e, "Exception occured while invoking batch completion delegate.");
            }
            finally
            {
                if (ctx != null)
                {
                    ctx.Dispose();
                }
            }
        }

        private static void HandleRequestCallCompletion(bool success, RequestCallContextSafeHandle ctx, RequestCallCompletionDelegate callback)
        {
            try
            {
                callback(success, ctx);
            }
            catch (Exception e)
            {
                Logger.Error(e, "Exception occured while invoking request call completion delegate.");
            }
            finally
            {
                if (ctx != null)
                {
                    ctx.Dispose();
                }
            }
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
