#region Copyright notice and license

// Copyright 2018 The gRPC Authors
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
using System.Buffers;

namespace Grpc.Core
{
    /// <summary>
    /// Provides access to the payload being deserialized when deserializing messages.
    /// </summary>
    public abstract class DeserializationContext
    {
        /// <summary>
        /// Get the total length of the payload in bytes.
        /// </summary>
        public abstract int PayloadLength { get; }

        /// <summary>
        /// Gets the entire payload as a newly allocated byte array.
        /// Once the byte array is returned, the byte array becomes owned by the caller and won't be ever accessed or reused by gRPC again.
        /// NOTE: Obtaining the buffer as a newly allocated byte array is the simplest way of accessing the payload,
        /// but it can have important consequences in high-performance scenarios.
        /// In particular, using this method usually requires copying of the entire buffer one extra time.
        /// Also, allocating a new buffer each time can put excessive pressure on GC, especially if
        /// the payload is more than 86700 bytes large (which means the newly allocated buffer will be placed in LOH,
        /// and LOH object can only be garbage collected via a full ("stop the world") GC run).
        /// NOTE: Deserializers are expected not to call this method more than once per received message
        /// (as there is no practical reason for doing so) and <c>DeserializationContext</c> implementations are free to assume so.
        /// </summary>
        /// <returns>byte array containing the entire payload.</returns>
        public virtual byte[] PayloadAsNewBuffer()
        {
            throw new NotImplementedException();
        }

        /// <summary>
        /// Gets the entire payload as a rented buffer.
        /// Caller is reponsible for disposing the rented buffer once the done processing it to signal that the buffer can be reclaimed by gRPC
        /// and used again for deserialization of another message.
        /// In most cases, using this method involves copying to the entire buffer (as the payload is internally
        /// delivered in multiple buffer segments and they need to be joined together to form a single monolithic buffer).
        /// On the other hand, this method is much more efficient in terms of GC pressure than <see cref="PayloadAsNewBuffer"/>
        /// as the buffer is reused rather than thrown away. This comes at the cost of the additional complexity where one needs
        /// to call <c>Dispose()</c> once done reading the data.
        /// </summary>
        /// <returns>a rented buffer the entire payload or null if payload is null.</returns>
        public virtual IMemoryOwner<byte> PayloadAsRentedBuffer()
        {
            throw new NotImplementedException();
        }

        // TODO(jtattermusch): better name that's more aligned with other methods?
        /// <summary>
        /// Tries to get next segment of the payload.
        /// This is the most efficient method of accessing the payload, no additional copying or allocation is made in most cases.
        /// Throws an exception if payload is null.
        /// </summary>
        /// <param name="bufferSegment">will be set to the next buffer segment if operation is successful.</param>
        /// <returns><c>true</c> the next segment was read, <c>false</c> if all segments have already been read.</returns>
        public virtual bool TryGetNextBufferSegment(out ReadOnlySpan<byte> bufferSegment)
        {
            throw new NotImplementedException();
        }
    }
}
