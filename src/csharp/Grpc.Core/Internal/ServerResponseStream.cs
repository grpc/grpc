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
using System.Threading.Tasks;
using Grpc.Core.Internal;

namespace Grpc.Core.Internal
{
    /// <summary>
    /// Writes responses asynchronously to an underlying AsyncCallServer object.
    /// </summary>
    internal class ServerResponseStream<TRequest, TResponse> : IServerStreamWriter<TResponse>, IServerResponseStream
        where TRequest : class
        where TResponse : class
    {
        readonly AsyncCallServer<TRequest, TResponse> call;
        WriteOptions writeOptions;

        public ServerResponseStream(AsyncCallServer<TRequest, TResponse> call)
        {
            this.call = call;
        }

        public Task WriteAsync(TResponse message)
        {
            return call.SendMessageAsync(message, GetWriteFlags());
        }

        public Task WriteResponseHeadersAsync(Metadata responseHeaders)
        {
            return call.SendInitialMetadataAsync(responseHeaders);
        }

        public WriteOptions WriteOptions
        {
            get
            {
                return writeOptions;
            }

            set
            {
                writeOptions = value;
            }
        }

        private WriteFlags GetWriteFlags()
        {
            var options = writeOptions;
            return options != null ? options.Flags : default(WriteFlags);
        }
    }
}
