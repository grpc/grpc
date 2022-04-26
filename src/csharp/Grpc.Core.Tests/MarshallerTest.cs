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

            public FakeDeserializationContext(byte[] payload)
            {
                this.payload = payload;
            }

            public override int PayloadLength => payload.Length;

            public override byte[] PayloadAsNewBuffer()
            {
                return payload;
            }
        }
    }
}
