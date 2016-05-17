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
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Grpc.Core.Logging;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    internal delegate void OpCompletionDelegate(bool success);

    internal delegate void BatchCompletionDelegate(bool success, BatchContextSafeHandle ctx);

    internal class CompletionRegistry
    {
        static readonly ILogger Logger = GrpcEnvironment.Logger.ForType<CompletionRegistry>();

        readonly GrpcEnvironment environment;
        readonly ConcurrentDictionary<IntPtr, OpCompletionDelegate> dict = new ConcurrentDictionary<IntPtr, OpCompletionDelegate>();

        public CompletionRegistry(GrpcEnvironment environment)
        {
            this.environment = environment;
        }

        public void Register(IntPtr key, OpCompletionDelegate callback)
        {
            environment.DebugStats.PendingBatchCompletions.Increment();
            GrpcPreconditions.CheckState(dict.TryAdd(key, callback));
        }

        public void RegisterBatchCompletion(BatchContextSafeHandle ctx, BatchCompletionDelegate callback)
        {
            OpCompletionDelegate opCallback = ((success) => HandleBatchCompletion(success, ctx, callback));
            Register(ctx.Handle, opCallback);
        }

        public OpCompletionDelegate Extract(IntPtr key)
        {
            OpCompletionDelegate value;
            GrpcPreconditions.CheckState(dict.TryRemove(key, out value));
            environment.DebugStats.PendingBatchCompletions.Decrement();
            return value;
        }

        private static void HandleBatchCompletion(bool success, BatchContextSafeHandle ctx, BatchCompletionDelegate callback)
        {
            try
            {
                callback(success, ctx);
            }
            catch (Exception e)
            {
                Logger.Error(e, "Exception occured while invoking completion delegate.");
            }
            finally
            {
                if (ctx != null)
                {
                    ctx.Dispose();
                }
            }
        }
    }
}
