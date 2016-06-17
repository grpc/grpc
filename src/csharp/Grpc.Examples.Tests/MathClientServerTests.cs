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
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Grpc.Core;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Math.Tests
{
    /// <summary>
    /// Math client talks to local math server.
    /// </summary>
    public class MathClientServerTest
    {
        const string Host = "localhost";
        Server server;
        Channel channel;
        Math.MathClient client;

        [TestFixtureSetUp]
        public void Init()
        {
            server = new Server
            {
                Services = { Math.BindService(new MathServiceImpl()) },
                Ports = { { Host, ServerPort.PickUnused, ServerCredentials.Insecure } }
            };
            server.Start();
            channel = new Channel(Host, server.Ports.Single().BoundPort, ChannelCredentials.Insecure);
            client = Math.NewClient(channel);
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            channel.ShutdownAsync().Wait();
            server.ShutdownAsync().Wait();
        }

        [Test]
        public void Div1()
        {
            DivReply response = client.Div(new DivArgs { Dividend = 10, Divisor = 3 });
            Assert.AreEqual(3, response.Quotient);
            Assert.AreEqual(1, response.Remainder);
        }

        [Test]
        public void Div2()
        {
            DivReply response = client.Div(new DivArgs { Dividend = 0, Divisor = 1 });
            Assert.AreEqual(0, response.Quotient);
            Assert.AreEqual(0, response.Remainder);
        }

        [Test]
        public void DivByZero()
        {
            var ex = Assert.Throws<RpcException>(() => client.Div(new DivArgs { Dividend = 0, Divisor = 0 }));
            Assert.AreEqual(StatusCode.InvalidArgument, ex.Status.StatusCode);
        }

        [Test]
        public async Task DivAsync()
        {
            DivReply response = await client.DivAsync(new DivArgs { Dividend = 10, Divisor = 3 });
            Assert.AreEqual(3, response.Quotient);
            Assert.AreEqual(1, response.Remainder);
        }

        [Test]
        public async Task Fib()
        {
            using (var call = client.Fib(new FibArgs { Limit = 6 }))
            {
                var responses = await call.ResponseStream.ToListAsync();
                CollectionAssert.AreEqual(new List<long> { 1, 1, 2, 3, 5, 8 },
                    responses.ConvertAll((n) => n.Num_));
            }
        }

        [Test]
        public async Task FibWithCancel()
        {
            var cts = new CancellationTokenSource();

            using (var call = client.Fib(new FibArgs { Limit = 0 }, cancellationToken: cts.Token))
            {
                List<long> responses = new List<long>();

                try
                {
                    while (await call.ResponseStream.MoveNext())
                    {
                        if (responses.Count == 0)
                        {
                            cts.CancelAfter(500);  // make sure we cancel soon
                        }
                        responses.Add(call.ResponseStream.Current.Num_);
                    }
                    Assert.Fail();
                }
                catch (RpcException e)
                {
                    Assert.IsTrue(responses.Count > 0);
                    Assert.AreEqual(StatusCode.Cancelled, e.Status.StatusCode);
                }
            }
        }

        [Test]
        public async Task FibWithDeadline()
        {
            using (var call = client.Fib(new FibArgs { Limit = 0 }, 
                deadline: DateTime.UtcNow.AddMilliseconds(500)))
            {
                var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.ToListAsync());

                // We can't guarantee the status code always DeadlineExceeded. See issue #2685.
                Assert.Contains(ex.Status.StatusCode, new[] { StatusCode.DeadlineExceeded, StatusCode.Internal });
            }
        }

        // TODO: test Fib with limit=0 and cancellation
        [Test]
        public async Task Sum()
        {
            using (var call = client.Sum())
            {
                var numbers = new List<long> { 10, 20, 30 }.ConvertAll(n => new Num { Num_ = n });

                await call.RequestStream.WriteAllAsync(numbers);
                var result = await call.ResponseAsync;
                Assert.AreEqual(60, result.Num_);
            }
        }

        [Test]
        public async Task DivMany()
        {
            var divArgsList = new List<DivArgs>
            {
                new DivArgs { Dividend = 10, Divisor = 3 },
                new DivArgs { Dividend = 100, Divisor = 21 },
                new DivArgs { Dividend = 7, Divisor = 2 }
            };

            using (var call = client.DivMany())
            {
                await call.RequestStream.WriteAllAsync(divArgsList);
                var result = await call.ResponseStream.ToListAsync();

                CollectionAssert.AreEqual(new long[] { 3, 4, 3 }, result.ConvertAll((divReply) => divReply.Quotient));
                CollectionAssert.AreEqual(new long[] { 1, 16, 1 }, result.ConvertAll((divReply) => divReply.Remainder));
            }
        }
    }
}
