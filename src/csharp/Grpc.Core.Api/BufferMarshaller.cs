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
using System.Buffers;
using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Encapsulates the logic for serializing and deserializing messages.
    /// </summary>
    public class BufferMarshaller<T> : Marshaller<T>
    {
        /// <summary>
        /// Initializes a new marshaller from simple serialize/deserialize functions.
        /// </summary>
        /// <param name="serializer">Function that will be used to serialize messages.</param>
        /// <param name="deserializer">Function that will be used to deserialize messages.</param>
        /// <param name="bufferSerializer"></param>
        /// <param name="bufferDeserializer"></param>
        public BufferMarshaller(Func<T, byte[]> serializer, Func<byte[], T> deserializer, Action<T, IBufferWriter<byte>> bufferSerializer, Func<ReadOnlySequence<byte>, T> bufferDeserializer)
            : base (serializer, deserializer)
        {
            WriteToBuffer = GrpcPreconditions.CheckNotNull(bufferSerializer, nameof(bufferSerializer));
            ReadFromBuffer = GrpcPreconditions.CheckNotNull(bufferDeserializer, nameof(bufferDeserializer));
        }

        /// <summary>
        /// 
        /// </summary>
        public Action<T, IBufferWriter<byte>> WriteToBuffer { get; }
        
        /// <summary>
        /// 
        /// </summary>
        public Func<ReadOnlySequence<byte>, T> ReadFromBuffer { get; }
    }

    /// <summary>
    /// Utilities for creating marshallers.
    /// </summary>
    public static class BufferMarshallers
    {
        /// <summary>
        /// Creates a marshaller from specified serializer and deserializer.
        /// </summary>
        public static BufferMarshaller<T> Create<T>(Func<T, byte[]> serializer, Func<byte[], T> deserializer, Action<T, IBufferWriter<byte>> bufferSerializer, Func<ReadOnlySequence<byte>, T> bufferDeserializer)
        {
            return new BufferMarshaller<T>(serializer, deserializer, bufferSerializer, bufferDeserializer);
        }
    }
}
