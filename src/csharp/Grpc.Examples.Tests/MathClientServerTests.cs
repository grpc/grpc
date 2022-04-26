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

        [OneTimeSetUp]
        public void Init()
        {
            // Disable SO_REUSEPORT to prevent https://github.com/grpc/grpc/issues/10755
            server = new Server(new[] { new ChannelOption(ChannelOptions.SoReuseport, 0) })
            {
                Services = { Math.BindService(new MathServiceImpl()) },
                Ports = { { Host, ServerPort.PickUnused, ServerCredentials.Insecure } }
            };
            server.Start();
            channel = new Channel(Host, server.Ports.Single().BoundPort, ChannelCredentials.Insecure);
            client = new Math.MathClient(channel);
        }

        [OneTimeTearDown]
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
                    responses.Select((n) => n.Num_));
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
        public void FibWithDeadline()
        {
            using (var call = client.Fib(new FibArgs { Limit = 0 }, 
                deadline: DateTime.UtcNow.AddMilliseconds(500)))
            {
                var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseStream.ToListAsync());
                Assert.AreEqual(StatusCode.DeadlineExceeded, ex.Status.StatusCode);
            }
        }

        // TODO: test Fib with limit=0 and cancellation
        [Test]
        public async Task Sum()
        {
            using (var call = client.Sum())
            {
                var numbers = new List<long> { 10, 20, 30 }.Select(n => new Num { Num_ = n });

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

                CollectionAssert.AreEqual(new long[] { 3, 4, 3 }, result.Select((divReply) => divReply.Quotient));
                CollectionAssert.AreEqual(new long[] { 1, 16, 1 }, result.Select((divReply) => divReply.Remainder));
            }
        }
    }
}
