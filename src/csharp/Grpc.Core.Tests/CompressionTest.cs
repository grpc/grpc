#region Copyright notice and license

// Copyright 2015, Google Inc.
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
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                context.WriteOptions = new WriteOptions(WriteFlags.NoCompress);
                return request;
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
