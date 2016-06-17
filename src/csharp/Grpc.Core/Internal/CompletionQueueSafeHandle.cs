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
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Grpc.Core.Profiling;

using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_completion_queue from <c>grpc/grpc.h</c>
    /// </summary>
    internal class CompletionQueueSafeHandle : SafeHandleZeroIsInvalid
    {
        static readonly NativeMethods Native = NativeMethods.Get();

        AtomicCounter shutdownRefcount = new AtomicCounter(1);
        CompletionRegistry completionRegistry;

        private CompletionQueueSafeHandle()
        {
        }

        public static CompletionQueueSafeHandle Create()
        {
            return Native.grpcsharp_completion_queue_create();
        }

        public static CompletionQueueSafeHandle Create(CompletionRegistry completionRegistry)
        {
            var cq = Native.grpcsharp_completion_queue_create();
            cq.completionRegistry = completionRegistry;
            return cq;
        }

        public CompletionQueueEvent Next()
        {
            return Native.grpcsharp_completion_queue_next(this);
        }

        public CompletionQueueEvent Pluck(IntPtr tag)
        {
            using (Profilers.ForCurrentThread().NewScope("CompletionQueueSafeHandle.Pluck"))
            {
                return Native.grpcsharp_completion_queue_pluck(this, tag);
            }
        }

        /// <summary>
        /// Creates a new usage scope for this completion queue. Once successfully created,
        /// the completion queue won't be shutdown before scope.Dispose() is called.
        /// </summary>
        public UsageScope NewScope()
        {
            return new UsageScope(this);
        }

        public void Shutdown()
        {
            DecrementShutdownRefcount();
        }

        /// <summary>
        /// Completion registry associated with this completion queue.
        /// Doesn't need to be set if only using Pluck() operations.
        /// </summary>
        public CompletionRegistry CompletionRegistry
        {
            get { return completionRegistry; }
        }

        protected override bool ReleaseHandle()
        {
            Native.grpcsharp_completion_queue_destroy(handle);
            return true;
        }

        private void DecrementShutdownRefcount()
        {
            if (shutdownRefcount.Decrement() == 0)
            {
                Native.grpcsharp_completion_queue_shutdown(this);
            }
        }

        private void BeginOp()
        {
            bool success = false;
            shutdownRefcount.IncrementIfNonzero(ref success);
            GrpcPreconditions.CheckState(success, "Shutdown has already been called");
        }

        private void EndOp()
        {
            DecrementShutdownRefcount();
        }

        // Allows declaring BeginOp and EndOp of a completion queue with a using statement.
        // Declared as struct for better performance.
        public struct UsageScope : IDisposable
        {
            readonly CompletionQueueSafeHandle cq;

            public UsageScope(CompletionQueueSafeHandle cq)
            {
                this.cq = cq;
                this.cq.BeginOp();
            }

            public void Dispose()
            {
                cq.EndOp();
            }
        }
    }
}
