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
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class MarshallerTest
    {
        [Test]
        public void ContextualSerializerEmulation()
        {
            Func<string, byte[]> simpleSerializer = System.Text.Encoding.UTF8.GetBytes;
            Func<byte[], string> simpleDeserializer = System.Text.Encoding.UTF8.GetString;
            var marshaller = new Marshaller<string>(simpleSerializer,
                                                    simpleDeserializer);

            Assert.AreSame(simpleSerializer, marshaller.Serializer);
            Assert.AreSame(simpleDeserializer, marshaller.Deserializer);

            // test that emulated contextual serializer and deserializer work
            string origMsg = "abc";
            var serializationContext = new FakeSerializationContext();
            marshaller.ContextualSerializer(origMsg, serializationContext);

            var deserializationContext = new FakeDeserializationContext(serializationContext.Payload);
            Assert.AreEqual(origMsg, marshaller.ContextualDeserializer(deserializationContext));
        }

        [Test]
        public void SimpleSerializerEmulation()
        {
            Action<string, SerializationContext> contextualSerializer = (str, context) =>
            {
                var bytes = System.Text.Encoding.UTF8.GetBytes(str);
                context.Complete(bytes);
            };
            Func<DeserializationContext, string> contextualDeserializer = (context) =>
            {
                return System.Text.Encoding.UTF8.GetString(context.PayloadAsNewBuffer());
            };
            var marshaller = new Marshaller<string>(contextualSerializer, contextualDeserializer);

            Assert.AreSame(contextualSerializer, marshaller.ContextualSerializer);
            Assert.AreSame(contextualDeserializer, marshaller.ContextualDeserializer);
            Assert.Throws(typeof(NotImplementedException), () => marshaller.Serializer("abc"));
            Assert.Throws(typeof(NotImplementedException), () => marshaller.Deserializer(new byte[] {1, 2, 3}));
        }

        [Test]
        public void ObviousArraySegmentDeserializationPatternIsRecognized()
        {
            var ctx = new FakeDeserializationContext(new byte[] { 0, 1, 2, 3, 4, 5 }, 1, 4);
            var obj = __Marshaller_BasicMessage.ContextualDeserializer(ctx);
#if GRPC_CSHARP_SUPPORT_SYSTEM_MEMORY
            // should have used the (byte[],int,int) method with an oversized original buffer
            Assert.AreEqual(6, obj.OriginalSegmentLength);
            Assert.IsTrue(obj.UsedSegmentDeserializer);
#else
            // platform fallback: we expect it to have used the (byte[]) method and a buffer copy
            Assert.AreEqual(4, obj.OriginalSegmentLength);
            Assert.IsFalse(obj.UsedSegmentDeserializer);
#endif
            Assert.AreEqual("01 02 03 04", obj.Payload);
        }

        // DO NOT CHANGE; this is a typical line as generate from protoc (stripped of the protobuf-specific bits)
        static readonly Marshaller<BasicMessage> __Marshaller_BasicMessage = Marshallers.Create((arg) => throw new NotImplementedException(), BasicMessage.Parser.ParseFrom);

        public sealed partial class BasicMessage
        {
            private static readonly FakeParser<BasicMessage> _parser = new FakeParser<BasicMessage>(() => new BasicMessage());
            public static FakeParser<BasicMessage> Parser { get { return _parser; } }

            public string Payload { get; set; }

            public int OriginalSegmentLength { get; set; }

            public bool UsedSegmentDeserializer {get;set;}

            public void Init(byte[] buffer, int offset, int count, bool fromSegment)
            {
                Payload = BitConverter.ToString(buffer, offset, count);
                OriginalSegmentLength = buffer.Length;
                UsedSegmentDeserializer = fromSegment;
            }
        }
        public class FakeParser<T>
        {
            private readonly Func<T> _factory;

            public FakeParser(Func<T> factory) => _factory = factory;

            // the extra methods here are important; we need to check we haven't done something silly like
            // adding something to the Marshaller ctor that would break the generated code
            public T ParseDelimitedFrom(Stream input) => throw new NotImplementedException();
            public T ParseFrom(byte[] data) => ParseFrom(data, 0, data.Length, false);
            public T ParseFrom(byte[] data, int offset, int length) => ParseFrom(data, offset, length, true);
            private T ParseFrom(byte[] data, int offset, int length, bool fromSegment)
            {
                var obj = _factory();
                if (obj is BasicMessage bm) bm.Init(data, offset, length, fromSegment);
                return obj;
            }
            public T ParseFrom(FakeByteString data) => throw new NotImplementedException();
            public T ParseFrom(Stream input) => throw new NotImplementedException();
            public T ParseFrom(FakeCodedInputStream input) => throw new NotImplementedException();
            public T ParseJson(string json) => throw new NotImplementedException();
            public FakeParser<T> WithDiscardUnknownFields(bool discardUnknownFields) => throw new NotImplementedException();
        }
        public class FakeCodedInputStream { }
        public class FakeByteString { }

        class FakeSerializationContext : SerializationContext
        {
            public byte[] Payload;
            public override void Complete(byte[] payload)
            {
                this.Payload = payload;
            }
        }

        class FakeDeserializationContext : DeserializationContext
        {
            public byte[] payload;
            public int offset, count;

            public FakeDeserializationContext(byte[] payload) : this(payload, 0, payload.Length) { }

            public FakeDeserializationContext(byte[] payload, int offset, int count)
            {
                this.payload = payload;
                this.offset = offset;
                this.count = count;
            }

            public override int PayloadLength => payload.Length;

            public override byte[] PayloadAsNewBuffer()
            {
                if (offset == 0 && count == payload.Length) return payload;
                var arr = new byte[count];
                Buffer.BlockCopy(payload, offset, arr, 0, count);
                return arr;
            }
#if GRPC_CSHARP_SUPPORT_SYSTEM_MEMORY
            public override ReadOnlySequence<byte> PayloadAsReadOnlySequence()
                => new ReadOnlySequence<byte>(payload, offset, count);
#endif
        }
    }
}
