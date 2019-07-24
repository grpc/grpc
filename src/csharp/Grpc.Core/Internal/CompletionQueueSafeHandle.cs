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

        AtomicCounter shutdownRefcount;
        CompletionRegistry completionRegistry;

        private CompletionQueueSafeHandle()
        {
        }

        /// <summary>
        /// Create a completion queue that can only be used for Pluck operations.
        /// </summary>
        public static CompletionQueueSafeHandle CreateSync()
        {
            return Native.grpcsharp_completion_queue_create_sync();
        }

        /// <summary>
        /// Create a completion queue that can only be used for Next operations.
        /// </summary>
        public static CompletionQueueSafeHandle CreateAsync(CompletionRegistry completionRegistry)
        {
            var cq = Native.grpcsharp_completion_queue_create_async();
            cq.completionRegistry = completionRegistry;
            cq.shutdownRefcount = new AtomicCounter(1);
            return cq;
        }

        public CompletionQueueEvent Next()
        {
            return Native.grpcsharp_completion_queue_next(this);
        }

        public CompletionQueueEvent Pluck(IntPtr tag)
        {
            return Native.grpcsharp_completion_queue_pluck(this, tag);
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
            if (shutdownRefcount == null || shutdownRefcount.Decrement() == 0)
            {
                Native.grpcsharp_completion_queue_shutdown(this);
            }
        }

        private void BeginOp()
        {
            GrpcPreconditions.CheckNotNull(shutdownRefcount, nameof(shutdownRefcount));
            bool success = false;
            shutdownRefcount.IncrementIfNonzero(ref success);
            GrpcPreconditions.CheckState(success, "Shutdown has already been called");
        }

        private void EndOp()
        {
            GrpcPreconditions.CheckNotNull(shutdownRefcount, nameof(shutdownRefcount));
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
