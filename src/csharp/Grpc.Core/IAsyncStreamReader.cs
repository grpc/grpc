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
