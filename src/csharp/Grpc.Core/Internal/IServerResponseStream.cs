#region Copyright notice and license
// Copyright 2019 The gRPC Authors
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
using System.Threading.Tasks;
using Grpc.Core.Internal;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Exposes non-generic members of <c>ServerReponseStream</c>.
    /// </summary>
    internal interface IServerResponseStream
    {
        /// <summary>
        /// Asynchronously sends response headers for the current call to the client. See <c>ServerCallContext.WriteResponseHeadersAsync</c> for exact semantics.
        /// </summary>
        Task WriteResponseHeadersAsync(Metadata responseHeaders);

        /// <summary>
        /// Gets or sets the write options.
        /// </summary>
        WriteOptions WriteOptions { get; set; }
    }
}
