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
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.Core.Tests
{
    public class CompressionTest
    {
        MockServiceHelper helper;
        Server server;
        Channel channel;

        [SetUp]
        public void Init()
        {
            helper = new MockServiceHelper();

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
        public void WriteOptions_Unary()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                context.WriteOptions = new WriteOptions(WriteFlags.NoCompress);
                return Task.FromResult(request);
            });

            var callOptions = new CallOptions(writeOptions: new WriteOptions(WriteFlags.NoCompress));
            Calls.BlockingUnaryCall(helper.CreateUnaryCall(callOptions), "abc");
        }

        [Test]
        public async Task WriteOptions_DuplexStreaming()
        {
            helper.DuplexStreamingHandler = new DuplexStreamingServerMethod<string, string>(async (requestStream, responseStream, context) =>
            {
                await requestStream.ToListAsync();

                context.WriteOptions = new WriteOptions(WriteFlags.NoCompress);

                await context.WriteResponseHeadersAsync(new Metadata { { "ascii-header", "abcdefg" } });

                await responseStream.WriteAsync("X");

                responseStream.WriteOptions = null;
                await responseStream.WriteAsync("Y");

                responseStream.WriteOptions = new WriteOptions(WriteFlags.NoCompress);
                await responseStream.WriteAsync("Z");
            });

            var callOptions = new CallOptions(writeOptions: new WriteOptions(WriteFlags.NoCompress));
            var call = Calls.AsyncDuplexStreamingCall(helper.CreateDuplexStreamingCall(callOptions));

            // check that write options from call options are propagated to request stream.
            Assert.IsTrue((call.RequestStream.WriteOptions.Flags & WriteFlags.NoCompress) != 0);

            call.RequestStream.WriteOptions = new WriteOptions();
            await call.RequestStream.WriteAsync("A");

            call.RequestStream.WriteOptions = null;
            await call.RequestStream.WriteAsync("B");

            call.RequestStream.WriteOptions = new WriteOptions(WriteFlags.NoCompress);
            await call.RequestStream.WriteAsync("C");

            await call.RequestStream.CompleteAsync();

            await call.ResponseStream.ToListAsync();
        }

        [Test]
        public void CanReadCompressedMessages()
        {
            var compressionMetadata = new Metadata
            {
                { new Metadata.Entry(Metadata.CompressionRequestAlgorithmMetadataKey, "gzip") }
            };

            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (req, context) =>
            {
                await context.WriteResponseHeadersAsync(compressionMetadata);
                return req;
            });

            var stringBuilder = new StringBuilder();
            for (int i = 0; i < 200000; i++)
            {
                stringBuilder.Append('a');
            }
            var request = stringBuilder.ToString();
            var response = Calls.BlockingUnaryCall(helper.CreateUnaryCall(new CallOptions(compressionMetadata)), request);

            Assert.AreEqual(request, response);
        }
    }
}
