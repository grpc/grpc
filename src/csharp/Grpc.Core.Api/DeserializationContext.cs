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
        /// NOTE: Deserializers are expected not to call this method (or other payload accessor methods) more than once per received message
        /// (as there is no practical reason for doing so) and <c>DeserializationContext</c> implementations are free to assume so.
        /// </summary>
        /// <returns>byte array containing the entire payload.</returns>
        public virtual byte[] PayloadAsNewBuffer()
        {
            throw new NotImplementedException();
        }

#if GRPC_CSHARP_SUPPORT_SYSTEM_MEMORY
        /// <summary>
        /// Gets the entire payload as a ReadOnlySequence.
        /// The ReadOnlySequence is only valid for the duration of the deserializer routine and the caller must not access it after the deserializer returns.
        /// Using the read only sequence is the most efficient way to access the message payload. Where possible it allows directly
        /// accessing the received payload without needing to perform any buffer copying or buffer allocations.
        /// NOTE: This method is only available in the netstandard2.0 build of the library.
        /// NOTE: Deserializers are expected not to call this method (or other payload accessor methods) more than once per received message
        /// (as there is no practical reason for doing so) and <c>DeserializationContext</c> implementations are free to assume so.
        /// </summary>
        /// <returns>read only sequence containing the entire payload.</returns>
        public virtual System.Buffers.ReadOnlySequence<byte> PayloadAsReadOnlySequence()
        {
            throw new NotImplementedException();
        }
#endif        
    }
}
