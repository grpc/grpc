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
using System.Collections.Generic;
using System.Threading.Tasks;

namespace Grpc.Core.Utils
{
    /// <summary>
    /// Extension methods that simplify work with gRPC streaming calls.
    /// </summary>
    public static class AsyncStreamExtensions
    {
        /// <summary>
        /// Reads the entire stream and executes an async action for each element.
        /// </summary>
        public static async Task ForEachAsync<T>(this IAsyncStreamReader<T> streamReader, Func<T, Task> asyncAction)
            where T : class
        {
            while (await streamReader.MoveNext().ConfigureAwait(false))
            {
                await asyncAction(streamReader.Current).ConfigureAwait(false);
            }
        }

        /// <summary>
        /// Reads the entire stream and creates a list containing all the elements read.
        /// </summary>
        public static async Task<List<T>> ToListAsync<T>(this IAsyncStreamReader<T> streamReader)
            where T : class
        {
            var result = new List<T>();
            while (await streamReader.MoveNext().ConfigureAwait(false))
            {
                result.Add(streamReader.Current);
            }
            return result;
        }

        /// <summary>
        /// Writes all elements from given enumerable to the stream.
        /// Completes the stream afterwards unless close = false.
        /// </summary>
        public static async Task WriteAllAsync<T>(this IClientStreamWriter<T> streamWriter, IEnumerable<T> elements, bool complete = true)
            where T : class
        {
            foreach (var element in elements)
            {
                await streamWriter.WriteAsync(element).ConfigureAwait(false);
            }
            if (complete)
            {
                await streamWriter.CompleteAsync().ConfigureAwait(false);
            }
        }

        /// <summary>
        /// Writes all elements from given enumerable to the stream.
        /// </summary>
        public static async Task WriteAllAsync<T>(this IServerStreamWriter<T> streamWriter, IEnumerable<T> elements)
            where T : class
        {
            foreach (var element in elements)
            {
                await streamWriter.WriteAsync(element).ConfigureAwait(false);
            }
        }
    }
}
