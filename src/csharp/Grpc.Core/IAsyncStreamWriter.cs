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
    /// A writable stream of messages.
    /// </summary>
    /// <typeparam name="T">The message type.</typeparam>
    public interface IAsyncStreamWriter<T>
    {
        /// <summary>
        /// Writes a single asynchronously. Only one write can be pending at a time.
        /// </summary>
        /// <param name="message">the message to be written. Cannot be null.</param>
        Task WriteAsync(T message);

        /// <summary>
        /// Write options that will be used for the next write.
        /// If null, default options will be used.
        /// Once set, this property maintains its value across subsequent
        /// writes.
        /// </summary>
        WriteOptions WriteOptions { get; set; }
    }
}
