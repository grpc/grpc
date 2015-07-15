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
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace math.Tests
{
    /// <summary>
    /// Math client talks to local math server.
    /// </summary>
    public class MathClientServerTest
    {
        string host = "localhost";
        Server server;
        Channel channel;
        Math.IMathClient client;

        [TestFixtureSetUp]
        public void Init()
        {
            server = new Server();
            server.AddServiceDefinition(Math.BindService(new MathServiceImpl()));
            int port = server.AddListeningPort(host, Server.PickUnusedPort);
            server.Start();
            channel = new Channel(host, port);

            // TODO(jtattermusch): get rid of the custom header here once we have dedicated tests
            // for header support.
            var stubConfig = new StubConfiguration((headerBuilder) =>
            {
                headerBuilder.Add(new Metadata.MetadataEntry("customHeader", "abcdef"));
            });
            client = Math.NewStub(channel, stubConfig);
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            channel.Dispose();
            server.ShutdownAsync().Wait();
            GrpcEnvironment.Shutdown();
        }

        [Test]
        public void Div1()
        {
            DivReply response = client.Div(new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build());
            Assert.AreEqual(3, response.Quotient);
            Assert.AreEqual(1, response.Remainder);
        }

        [Test]
        public void Div2()
        {
            DivReply response = client.Div(new DivArgs.Builder { Dividend = 0, Divisor = 1 }.Build());
            Assert.AreEqual(0, response.Quotient);
            Assert.AreEqual(0, response.Remainder);
        }

        [Test]
        public void DivByZero()
        {
            try
            {
                DivReply response = client.Div(new DivArgs.Builder { Dividend = 0, Divisor = 0 }.Build());
                Assert.Fail();
            }
            catch (RpcException e)
            {
                Assert.AreEqual(StatusCode.Unknown, e.Status.StatusCode);
            }   
        }

        [Test]
        public void DivAsync()
        {
            Task.Run(async () =>
            {
                DivReply response = await client.DivAsync(new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build());
                Assert.AreEqual(3, response.Quotient);
                Assert.AreEqual(1, response.Remainder);
            }).Wait();
        }

        [Test]
        public void Fib()
        {
            Task.Run(async () =>
            {
                using (var call = client.Fib(new FibArgs.Builder { Limit = 6 }.Build()))
                {
                    var responses = await call.ResponseStream.ToList();
                    CollectionAssert.AreEqual(new List<long> { 1, 1, 2, 3, 5, 8 },
                        responses.ConvertAll((n) => n.Num_));
                }
            }).Wait();
        }

        // TODO: test Fib with limit=0 and cancellation
        [Test]
        public void Sum()
        {
            Task.Run(async () =>
            {
                using (var call = client.Sum())
                {
                    var numbers = new List<long> { 10, 20, 30 }.ConvertAll(
                             n => Num.CreateBuilder().SetNum_(n).Build());

                    await call.RequestStream.WriteAll(numbers);
                    var result = await call.Result;
                    Assert.AreEqual(60, result.Num_);
                }
            }).Wait();
        }

        [Test]
        public void DivMany()
        {
            Task.Run(async () =>
            {
                var divArgsList = new List<DivArgs>
                {
                    new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build(),
                    new DivArgs.Builder { Dividend = 100, Divisor = 21 }.Build(),
                    new DivArgs.Builder { Dividend = 7, Divisor = 2 }.Build()
                };

                using (var call = client.DivMany())
                {
                    await call.RequestStream.WriteAll(divArgsList);
                    var result = await call.ResponseStream.ToList();

                    CollectionAssert.AreEqual(new long[] { 3, 4, 3 }, result.ConvertAll((divReply) => divReply.Quotient));
                    CollectionAssert.AreEqual(new long[] { 1, 16, 1 }, result.ConvertAll((divReply) => divReply.Remainder));
                }
            }).Wait();
        }
    }
}
