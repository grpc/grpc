﻿#region Copyright notice and license

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
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Grpc.Core
{
    /// <summary>
    /// A stream of messages to be read.
    /// Messages can be awaited <c>await reader.MoveNext()</c>, that returns <c>true</c>
    /// if there is a message available and <c>false</c> if there are no more messages
    /// (i.e. the stream has been closed).
    /// <para>
    /// On the client side, the last invocation of <c>MoveNext()</c> either returns <c>false</c>
    /// if the call has finished successfully or throws <c>RpcException</c> if call finished
    /// with an error. Once the call finishes, subsequent invocations of <c>MoveNext()</c> will
    /// continue yielding the same result (returning <c>false</c> or throwing an exception).
    /// </para>
    /// <para>
    /// On the server side, <c>MoveNext()</c> does not throw exceptions.
    /// In case of a failure, the request stream will appear to be finished
    /// (<c>MoveNext</c> will return <c>false</c>) and the <c>CancellationToken</c>
    /// associated with the call will be cancelled to signal the failure.
    /// </para>
    /// </summary>
    /// <typeparam name="T">The message type.</typeparam>
    public interface IAsyncStreamReader<T> : IAsyncEnumerator<T>
    {
    }
}
