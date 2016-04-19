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
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Core.Internal;
using Grpc.Core.Utils;

using NUnit.Framework;

namespace Grpc.Core.Tests
{
    /// <summary>
    /// Tests for response headers support.
    /// </summary>
    public class ResponseHeadersTest
    {
        MockServiceHelper helper;
        Server server;
        Channel channel;

        Metadata headers;

        [SetUp]
        public void Init()
        {
            helper = new MockServiceHelper();

            server = helper.GetServer();
            server.Start();
            channel = helper.GetChannel();

            headers = new Metadata { { "ascii-header", "abcdefg" } };
        }

        [TearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public async Task ResponseHeadersAsync_UnaryCall()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                await context.WriteResponseHeadersAsync(headers);
                return "PASS";
            });

            var call = Calls.AsyncUnaryCall(helper.CreateUnaryCall(), "");
            var responseHeaders = await call.ResponseHeadersAsync;

            Assert.AreEqual(headers.Count, responseHeaders.Count);
            Assert.AreEqual("ascii-header", responseHeaders[0].Key);
            Assert.AreEqual("abcdefg", responseHeaders[0].Value);

            Assert.AreEqual("PASS", await call.ResponseAsync);
        }

        [Test]
        public async Task ResponseHeadersAsync_ClientStreamingCall()
        {
            helper.ClientStreamingHandler = new ClientStreamingServerMethod<string, string>(async (requestStream, context) =>
            {
                await context.WriteResponseHeadersAsync(headers);
                return "PASS";
            });

            var call = Calls.AsyncClientStreamingCall(helper.CreateClientStreamingCall());
            await call.RequestStream.CompleteAsync();
            var responseHeaders = await call.ResponseHeadersAsync;

            Assert.AreEqual("ascii-header", responseHeaders[0].Key);
            Assert.AreEqual("PASS", await call.ResponseAsync);
        }

        [Test]
        public async Task ResponseHeadersAsync_ServerStreamingCall()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) =>
            {
                await context.WriteResponseHeadersAsync(headers);
                await responseStream.WriteAsync("PASS");
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "");
            var responseHeaders = await call.ResponseHeadersAsync;

            Assert.AreEqual("ascii-header", responseHeaders[0].Key);
            CollectionAssert.AreEqual(new[] { "PASS" }, await call.ResponseStream.ToListAsync());
        }

        [Test]
        public async Task ResponseHeadersAsync_DuplexStreamingCall()
        {
            helper.DuplexStreamingHandler = new DuplexStreamingServerMethod<string, string>(async (requestStream, responseStream, context) =>
            {
                await context.WriteResponseHeadersAsync(headers);
                while (await requestStream.MoveNext())
                {
                    await responseStream.WriteAsync(requestStream.Current);
                }
            });

            var call = Calls.AsyncDuplexStreamingCall(helper.CreateDuplexStreamingCall());
            var responseHeaders = await call.ResponseHeadersAsync;

            var messages = new[] { "PASS" };
            await call.RequestStream.WriteAllAsync(messages);

            Assert.AreEqual("ascii-header", responseHeaders[0].Key);
            CollectionAssert.AreEqual(messages, await call.ResponseStream.ToListAsync());
        }

        [Test]
        public void WriteResponseHeaders_NullNotAllowed()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                Assert.ThrowsAsync(typeof(ArgumentNullException), async () => await context.WriteResponseHeadersAsync(null));
                return "PASS";
            });

            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), ""));
        }

        [Test]
        public void WriteResponseHeaders_AllowedOnlyOnce()
        {
            helper.UnaryHandler = new UnaryServerMethod<string, string>(async (request, context) =>
            {
                await context.WriteResponseHeadersAsync(headers);
                try
                {
                    await context.WriteResponseHeadersAsync(headers);
                    Assert.Fail();
                }
                catch (InvalidOperationException expected)
                {
                }
                return "PASS";
            });
                
            Assert.AreEqual("PASS", Calls.BlockingUnaryCall(helper.CreateUnaryCall(), ""));
        }

        [Test]
        public async Task WriteResponseHeaders_NotAllowedAfterWrite()
        {
            helper.ServerStreamingHandler = new ServerStreamingServerMethod<string, string>(async (request, responseStream, context) =>
            {
                await responseStream.WriteAsync("A");
                try
                {
                    await context.WriteResponseHeadersAsync(headers);
                    Assert.Fail();
                }
                catch (InvalidOperationException expected)
                {
                }
                await responseStream.WriteAsync("B");
            });

            var call = Calls.AsyncServerStreamingCall(helper.CreateServerStreamingCall(), "");
            var responses = await call.ResponseStream.ToListAsync();
            CollectionAssert.AreEqual(new[] { "A", "B" }, responses);
        }
    }
}
