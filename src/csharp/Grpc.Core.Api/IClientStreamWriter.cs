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
    /// Client-side writable stream of messages with Close capability.
    /// </summary>
    /// <typeparam name="T">The message type.</typeparam>
    public interface IClientStreamWriter<T> : IAsyncStreamWriter<T>
    {
        /// <summary>
        /// Completes/closes the stream. Can only be called once there is no pending write. No writes should follow calling this.
        /// </summary>
        Task CompleteAsync();
    }
}
