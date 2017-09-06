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
            helper.UnaryHandler = new UnaryServerMethod<string, string>((request, context) =>
            {
                Assert.ThrowsAsync(typeof(ArgumentNullException), async () => await context.WriteResponseHeadersAsync(null));
                return Task.FromResult("PASS");
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
                catch (InvalidOperationException)
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
                catch (InvalidOperationException)
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
