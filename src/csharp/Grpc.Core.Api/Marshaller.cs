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
using System.Reflection;
using Grpc.Core.Utils;
using System.Runtime.InteropServices;

#if GRPC_CSHARP_SUPPORT_SYSTEM_MEMORY
using System.Buffers;
#endif

namespace Grpc.Core
{
    /// <summary>
    /// Encapsulates the logic for serializing and deserializing messages.
    /// </summary>
    public class Marshaller<T>
    {
        readonly Func<T, byte[]> serializer;
        readonly Func<byte[], T> deserializer;

        readonly Action<T, SerializationContext> contextualSerializer;
        readonly Func<DeserializationContext, T> contextualDeserializer;

        /// <summary>
        /// Initializes a new marshaller from simple serialize/deserialize functions.
        /// </summary>
        /// <param name="serializer">Function that will be used to serialize messages.</param>
        /// <param name="deserializer">Function that will be used to deserialize messages.</param>
        public Marshaller(Func<T, byte[]> serializer, Func<byte[], T> deserializer)
        {
            this.serializer = GrpcPreconditions.CheckNotNull(serializer, nameof(serializer));
            this.deserializer = GrpcPreconditions.CheckNotNull(deserializer, nameof(deserializer));

            // contextual serialization/deserialization is emulated to make the marshaller
            // usable with the grpc library (required for backward compatibility).
            this.contextualSerializer = EmulateContextualSerializer;

            if (!TryFindArraySegmentDeserializer(deserializer, out this.contextualDeserializer))
            {
                this.contextualDeserializer = EmulateContextualDeserializer;
            }
        }

        /// <summary>
        /// Given a deserializer of the form foo.Bar(byte[]), see if we can find a similar foo.Bar(byte[], int, int); if so,
        /// prefer that; this allows the existing protoc code-gen to be identified to use pooled buffers
        /// </summary>
        private static bool TryFindArraySegmentDeserializer(Func<byte[], T> deserializer, out Func<DeserializationContext, T> contextualDeserializer)
        {
#if GRPC_CSHARP_SUPPORT_SYSTEM_MEMORY
            try
            {
                if (deserializer.GetInvocationList().Length == 1)
                {
                    var method = deserializer.Method;
                    var flags = (deserializer.Target == null ? BindingFlags.Static : BindingFlags.Instance) | BindingFlags.Public;
                    var bySegmentMethod = method.DeclaringType.GetMethod(method.Name, flags, null, s_arraySegmentSignature, null);
                    if (bySegmentMethod != null && bySegmentMethod.ReturnType == typeof(T))
                    {
                        // we're going to be picky about what signatures we detect here for special treatment; the
                        // next two parameters must be "offset, length" or "offset, count" - just to be *really clear*
                        // that we understand the intent of that method
                        var parameters = bySegmentMethod.GetParameters();
                        if (parameters.Length == 3 &&
                            StringComparer.OrdinalIgnoreCase.Equals(parameters[1].Name, "offset")
                            && (StringComparer.OrdinalIgnoreCase.Equals(parameters[2].Name, "length")
                                || StringComparer.OrdinalIgnoreCase.Equals(parameters[2].Name, "count")))
                        {
                            var bySegmentTyped = (Func<byte[], int, int, T>)Delegate.CreateDelegate(typeof(Func<byte[], int, int, T>), deserializer.Target, bySegmentMethod);
                            contextualDeserializer = ctx => {
                                var len = ctx.PayloadLength;
                                if (len == 0)
                                {
#if NET45
                                    return bySegmentTyped(s_empty, 0, 0);
#else
                                return bySegmentTyped(Array.Empty<byte>(), 0, 0);
#endif
                                }
                                var ros = ctx.PayloadAsReadOnlySequence();
                                if (ros.IsSingleSegment && MemoryMarshal.TryGetArray(ros.First, out var segment)
                                    && segment.Count == len)
                                {
                                    // the data is *already* a single segment that is an array; use directly
                                    return bySegmentTyped(segment.Array, segment.Offset, len);
                                }
                                else
                                {
                                    // lease a buffer from the pool, copy into that buffer, and deserialize
                                    // from that, recycling the buffer when done
                                    var arr = ArrayPool<byte>.Shared.Rent(len);
                                    try
                                    {
                                        ros.CopyTo(arr);
                                        return bySegmentTyped(arr, 0, len);
                                    }
                                    finally
                                    {
                                        ArrayPool<byte>.Shared.Return(arr);
                                    }
                                }
                            };
                            return true;
                        }
                    }
                }
            } catch { } // give up; best efforts only
#endif
            contextualDeserializer = default;
            return false;
        }

#if NET45
        private static readonly byte[] s_empty = new byte[0]; // Array.Empty not available on all target platforms
#endif
        private static readonly Type[] s_arraySegmentSignature = new Type[] { typeof(byte[]), typeof(int), typeof(int) };

        /// <summary>
        /// Initializes a new marshaller from serialize/deserialize fuctions that can access serialization and deserialization
        /// context. Compared to the simple serializer/deserializer functions, using the contextual version provides more
        /// flexibility and can lead to increased efficiency (and better performance).
        /// Note: This constructor is part of an experimental API that can change or be removed without any prior notice.
        /// </summary>
        /// <param name="serializer">Function that will be used to serialize messages.</param>
        /// <param name="deserializer">Function that will be used to deserialize messages.</param>
        public Marshaller(Action<T, SerializationContext> serializer, Func<DeserializationContext, T> deserializer)
        {
            this.contextualSerializer = GrpcPreconditions.CheckNotNull(serializer, nameof(serializer));
            this.contextualDeserializer = GrpcPreconditions.CheckNotNull(deserializer, nameof(deserializer));
            // gRPC only uses contextual serializer/deserializer internally, so emulating the legacy
            // (de)serializer is not necessary.
            this.serializer = (msg) => { throw new NotImplementedException(); };
            this.deserializer = (payload) => { throw new NotImplementedException(); };
        }

        /// <summary>
        /// Gets the serializer function.
        /// </summary>
        public Func<T, byte[]> Serializer => this.serializer;

        /// <summary>
        /// Gets the deserializer function.
        /// </summary>
        public Func<byte[], T> Deserializer => this.deserializer;

        /// <summary>
        /// Gets the serializer function.
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        public Action<T, SerializationContext> ContextualSerializer => this.contextualSerializer;

        /// <summary>
        /// Gets the serializer function.
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        public Func<DeserializationContext, T> ContextualDeserializer => this.contextualDeserializer;

        // for backward compatibility, emulate the contextual serializer using the simple one
        private void EmulateContextualSerializer(T message, SerializationContext context)
        {
            var payload = this.serializer(message);
            context.Complete(payload);
        }

        // for backward compatibility, emulate the contextual deserializer using the simple one
        private T EmulateContextualDeserializer(DeserializationContext context)
        {
            return this.deserializer(context.PayloadAsNewBuffer());
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
        /// Creates a marshaller from specified contextual serializer and deserializer.
        /// Note: This method is part of an experimental API that can change or be removed without any prior notice.
        /// </summary>
        public static Marshaller<T> Create<T>(Action<T, SerializationContext> serializer, Func<DeserializationContext, T> deserializer)
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
