#region Copyright notice and license

// Copyright 2015-2016, Google Inc.
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
