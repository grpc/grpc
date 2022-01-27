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
    public class ContextualMarshallerTest
    {
        const string Host = "127.0.0.1";

        MockServiceHelper helper;
        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            var contextualMarshaller = new Marshaller<string>(
                (str, serializationContext) =>
                {
                    if (str == "UNSERIALIZABLE_VALUE")
                    {
                        // Google.Protobuf throws exception inherited from IOException
                        throw new IOException("Error serializing the message.");
                    }
                    if (str == "SERIALIZE_TO_NULL")
                    {
                        // for contextual marshaller, serializing to null payload corresponds
                        // to not calling the Complete() method in the serializer.
                        return;
                    }
                    var bytes = System.Text.Encoding.UTF8.GetBytes(str);
                    serializationContext.Complete(bytes);
                },
                (deserializationContext) =>
                {
                    var buffer = deserializationContext.PayloadAsNewBuffer();
                    Assert.AreEqual(buffer.Length, deserializationContext.PayloadLength);
                    var s = System.Text.Encoding.UTF8.GetString(buffer);
                    if (s == "UNPARSEABLE_VALUE")
                    {
                        // Google.Protobuf throws exception inherited from IOException
                        throw new IOException("Error parsing the message.");
                    }
                    return s;
                });
            helper = new MockServiceHelper(Host, contextualMarshaller);
            server = helper.GetServer();
            server.Start();
            channel = helper.GetChannel();
        }

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void UnaryCall()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult(request);
            });
            Assert.AreEqual("ABC", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "ABC"));
        }

        [Test]
        public void ResponseParsingError_UnaryResponse()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                return Task.FromResult("UNPARSEABLE_VALUE");
            });

            var ex = Assert.Throws<RpcException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "REQUEST"));
            Assert.AreEqual(StatusCode.Internal, ex.Status.StatusCode);
        }

        [Test]
        public void RequestSerializationError_BlockingUnary()
        {
            Assert.Throws<IOException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "UNSERIALIZABLE_VALUE"));
        }

        [Test]
        public void SerializationResultIsNull_BlockingUnary()
        {
            Assert.Throws<NullReferenceException>(() => Calls.BlockingUnaryCall(helper.CreateUnaryCall(), "SERIALIZE_TO_NULL"));
        }
    }
}
