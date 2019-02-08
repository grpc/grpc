#region Copyright notice and license

// Copyright 2015-2016 gRPC authors.
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

namespace Grpc.Core
{
    /// <summary>
    /// Flags for write operations.
    /// </summary>
    [Flags]
    public enum WriteFlags
    {
        /// <summary>
        /// Hint that the write may be buffered and need not go out on the wire immediately.
        /// gRPC is free to buffer the message until the next non-buffered
        /// write, or until write stream completion, but it need not buffer completely or at all.
        /// </summary>
        BufferHint = 0x1,

        /// <summary>
        /// Force compression to be disabled for a particular write.
        /// </summary>
        NoCompress = 0x2
    }

    /// <summary>
    /// Options for write operations.
    /// </summary>
    public class WriteOptions
    {
        /// <summary>
        /// Default write options.
        /// </summary>
        public static readonly WriteOptions Default = new WriteOptions();

        private readonly WriteFlags flags;

        /// <summary>
        /// Initializes a new instance of <c>WriteOptions</c> class.
        /// </summary>
        /// <param name="flags">The write flags.</param>
        public WriteOptions(WriteFlags flags = default(WriteFlags))
        {
            this.flags = flags;
        }

        /// <summary>
        /// Gets the write flags.
        /// </summary>
        public WriteFlags Flags
        {
            get
            {
                return this.flags;
            }
        }
    }
}
