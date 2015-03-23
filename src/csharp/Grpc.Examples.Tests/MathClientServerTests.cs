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
        MathGrpc.IMathServiceClient client;

        [TestFixtureSetUp]
        public void Init()
        {
            GrpcEnvironment.Initialize();

            server = new Server();
            server.AddServiceDefinition(MathGrpc.BindService(new MathServiceImpl()));
            int port = server.AddListeningPort(host + ":0");
            server.Start();
            channel = new Channel(host + ":" + port);

            // TODO: get rid of the custom header here once we have dedicated tests
            // for header support.
            var stubConfig = new StubConfiguration((headerBuilder) =>
            {
                headerBuilder.Add(new Metadata.MetadataEntry("customHeader", "abcdef"));
            });
            client = MathGrpc.NewStub(channel, stubConfig);
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

        // TODO: test division by zero

        [Test]
        public void DivAsync()
        {
            DivReply response = client.DivAsync(new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build()).Result;
            Assert.AreEqual(3, response.Quotient);
            Assert.AreEqual(1, response.Remainder);
        }

        [Test]
        public void Fib()
        {
            var recorder = new RecordingObserver<Num>();
            client.Fib(new FibArgs.Builder { Limit = 6 }.Build(), recorder);

            CollectionAssert.AreEqual(new List<long> { 1, 1, 2, 3, 5, 8 },
                recorder.ToList().Result.ConvertAll((n) => n.Num_));
        }

        // TODO: test Fib with limit=0 and cancellation
        [Test]
        public void Sum()
        {
            var res = client.Sum();
            foreach (var num in new long[] { 10, 20, 30 })
            {
                res.Inputs.OnNext(Num.CreateBuilder().SetNum_(num).Build());
            }
            res.Inputs.OnCompleted();

            Assert.AreEqual(60, res.Task.Result.Num_);
        }

        [Test]
        public void DivMany()
        {
            List<DivArgs> divArgsList = new List<DivArgs>
            {
                new DivArgs.Builder { Dividend = 10, Divisor = 3 }.Build(),
                new DivArgs.Builder { Dividend = 100, Divisor = 21 }.Build(),
                new DivArgs.Builder { Dividend = 7, Divisor = 2 }.Build()
            };

            var recorder = new RecordingObserver<DivReply>();
            var requestObserver = client.DivMany(recorder);

            foreach (var arg in divArgsList)
            {
                requestObserver.OnNext(arg);
            }
            requestObserver.OnCompleted();

            var result = recorder.ToList().Result;

            CollectionAssert.AreEqual(new long[] { 3, 4, 3 }, result.ConvertAll((divReply) => divReply.Quotient));
            CollectionAssert.AreEqual(new long[] { 1, 16, 1 }, result.ConvertAll((divReply) => divReply.Remainder));
        }
    }
}
