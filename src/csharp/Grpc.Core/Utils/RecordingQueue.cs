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
using System.Threading.Tasks;

namespace Grpc.Core.Utils
{
    // TODO: replace this by something that implements IAsyncEnumerator.
    /// <summary>
    /// Observer that allows us to await incoming messages one-by-one.
    /// The implementation is not ideal and class will be probably replaced
    /// by something more versatile in the future.
    /// </summary>
    public class RecordingQueue<T> : IObserver<T>
    {
        readonly BlockingCollection<T> queue = new BlockingCollection<T>();
        TaskCompletionSource<object> tcs = new TaskCompletionSource<object>();

        public void OnCompleted()
        {
            tcs.SetResult(null);
        }

        public void OnError(Exception error)
        {
            tcs.SetException(error);
        }

        public void OnNext(T value)
        {
            queue.Add(value);
        }

        public BlockingCollection<T> Queue
        {
            get
            {
                return queue;
            }
        }

        public Task Finished
        {
            get
            {
                return tcs.Task;
            }
        }
    }
}
