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
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// If error != null, there's been an error or operation has been cancelled.
    /// </summary>
    internal delegate void AsyncCompletionDelegate<T>(T result, Exception error);

    /// <summary>
    /// Helper for transforming AsyncCompletionDelegate into full-fledged Task.
    /// </summary>
    internal class AsyncCompletionTaskSource<T>
    {
        readonly TaskCompletionSource<T> tcs = new TaskCompletionSource<T>();
        readonly AsyncCompletionDelegate<T> completionDelegate;

        public AsyncCompletionTaskSource()
        {
            completionDelegate = new AsyncCompletionDelegate<T>(HandleCompletion);
        }

        public Task<T> Task
        {
            get
            {
                return tcs.Task;
            }
        }

        public AsyncCompletionDelegate<T> CompletionDelegate
        {
            get
            {
                return completionDelegate;
            }
        }

        private void HandleCompletion(T value, Exception error)
        {
            if (error == null)
            {
                tcs.SetResult(value);
                return;
            }
            if (error is OperationCanceledException)
            {
                tcs.SetCanceled();
                return;
            }
            tcs.SetException(error);
        }
    }
}
