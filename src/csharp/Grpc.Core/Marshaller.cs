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
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Encapsulates the logic for serializing and deserializing messages.
    /// </summary>
    public class Marshaller<T>
    {
        readonly Func<T, byte[]> serializer;
        readonly Func<byte[], T> deserializer;
        readonly Func<ArraySegment<byte>, T> segmentDeserializer;

        /// <summary>
        /// Initializes a new marshaller.
        /// </summary>
        /// <param name="serializer">Function that will be used to serialize messages.</param>
        /// <param name="deserializer">Function that will be used to deserialize messages.</param>
        public Marshaller(Func<T, byte[]> serializer, Func<byte[], T> deserializer)
        {
            this.serializer = GrpcPreconditions.CheckNotNull(serializer, nameof(serializer));
            this.deserializer = GrpcPreconditions.CheckNotNull(deserializer, nameof(deserializer));
            this.segmentDeserializer = (arraySegment) => this.deserializer(CopyToNewByteArray(arraySegment));
        }

        /// <summary>
        /// Initializes a new marshaller.
        /// </summary>
        public Marshaller(Func<T, byte[]> serializer, Func<ArraySegment<byte>, T> segmentDeserializer)
        {
            this.serializer = GrpcPreconditions.CheckNotNull(serializer, nameof(serializer));
            this.segmentDeserializer = GrpcPreconditions.CheckNotNull(segmentDeserializer, nameof(segmentDeserializer));
            this.deserializer = (bytes) => this.segmentDeserializer(new ArraySegment<byte>(bytes));
        }

        /// <summary>
        /// Gets the serializer function.
        /// </summary>
        public Func<T, byte[]> Serializer
        {
            get
            {
                return this.serializer;
            }
        }

        /// <summary>
        /// Gets the deserializer function.
        /// </summary>
        public Func<byte[], T> Deserializer
        {
            get
            {
                return this.deserializer;
            }
        }

        /// <summary>
        /// Get the segment deserialize function.
        /// IMPORTANT: The <c>ArraySegment</c> containing the data to deserialize
        /// is only guaranteed to exist (and contain meaningul data) over the course
        /// of deserializer's invocation. The deserializer's implementation must not keep
        /// any references to the input data once it returns (the buffer will likely
        /// get reused).
        /// </summary>
        public Func<ArraySegment<byte>, T> ArraySegmentDeserializer
        {
            get
            {
                return this.segmentDeserializer;
            }
        }

        private static byte[] CopyToNewByteArray(ArraySegment<byte> segment)
        {
            var bytes = new byte[segment.Count];
            Buffer.BlockCopy(segment.Array, segment.Offset, bytes, 0, segment.Count);
            return bytes;
        }
    }

    /// <summary>
    /// Utilities for creating marshallers.
    /// </summary>
    public static class Marshallers
    {
        /// <summary>
        /// Creates a marshaller from specified serializer and deserializer.
        /// </summary>
        public static Marshaller<T> Create<T>(Func<T, byte[]> serializer, Func<byte[], T> deserializer)
        {
            return new Marshaller<T>(serializer, deserializer);
        }

        /// <summary>
        /// Creates a marshaller from specified serializer and deserializer.
        /// </summary>
        public static Marshaller<T> Create<T>(Func<T, byte[]> serializer, Func<ArraySegment<byte>, T> deserializer)
        {
            return new Marshaller<T>(serializer, deserializer);
        }

        /// <summary>
        /// Returns a marshaller for <c>string</c> type. This is useful for testing.
        /// </summary>
        public static Marshaller<string> StringMarshaller
        {
            get
            {
                return new Marshaller<string>(System.Text.Encoding.UTF8.GetBytes,
                                              System.Text.Encoding.UTF8.GetString);
            }
        }
    }
}
