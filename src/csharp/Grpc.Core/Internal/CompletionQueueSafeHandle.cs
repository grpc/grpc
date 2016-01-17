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

namespace Grpc.Core.Internal
{
    /// <summary>
    /// grpc_completion_queue from <c>grpc/grpc.h</c>
    /// </summary>
    internal class CompletionQueueSafeHandle : SafeHandleZeroIsInvalid
    {
        [DllImport("grpc_csharp_ext.dll")]
        static extern CompletionQueueSafeHandle grpcsharp_completion_queue_create();

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_completion_queue_shutdown(CompletionQueueSafeHandle cq);

        [DllImport("grpc_csharp_ext.dll")]
        static extern CompletionQueueEvent grpcsharp_completion_queue_next(CompletionQueueSafeHandle cq);

        [DllImport("grpc_csharp_ext.dll")]
        static extern CompletionQueueEvent grpcsharp_completion_queue_pluck(CompletionQueueSafeHandle cq, IntPtr tag);

        [DllImport("grpc_csharp_ext.dll")]
        static extern void grpcsharp_completion_queue_destroy(IntPtr cq);

        private CompletionQueueSafeHandle()
        {
        }

        public static CompletionQueueSafeHandle Create()
        {
            return grpcsharp_completion_queue_create();
        }

        public CompletionQueueEvent Next()
        {
            return grpcsharp_completion_queue_next(this);
        }

        public CompletionQueueEvent Pluck(IntPtr tag)
        {
            using (Profilers.ForCurrentThread().NewScope("CompletionQueueSafeHandle.Pluck"))
            {
                return grpcsharp_completion_queue_pluck(this, tag);
            }
        }

        public void Shutdown()
        {
            grpcsharp_completion_queue_shutdown(this);
        }

        protected override bool ReleaseHandle()
        {
            grpcsharp_completion_queue_destroy(handle);
            return true;
        }
    }
}
