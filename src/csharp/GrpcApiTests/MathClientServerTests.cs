using System;
using NUnit.Framework;
using Google.GRPC.Core;
using System.Threading;
using System.Threading.Tasks;
using Google.GRPC.Core.Utils;
using System.Collections.Generic;

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
            int port = server.AddPort(host + ":0");
            server.Start();
            channel = new Channel(host + ":" + port);
            client = MathGrpc.NewStub(channel);
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

            CollectionAssert.AreEqual(new List<long>{1, 1, 2, 3, 5, 8}, 
                recorder.ToList().Result.ConvertAll((n) => n.Num_));
        }

        // TODO: test Fib with limit=0 and cancellation
        [Test]
        public void Sum()
        {
            var res = client.Sum();
            foreach (var num in new long[] { 10, 20, 30 }) {
                res.Inputs.OnNext(Num.CreateBuilder().SetNum_(num).Build());
            }
            res.Inputs.OnCompleted();

            Assert.AreEqual(60, res.Task.Result.Num_);
        }

        [Test]
        public void DivMany()
        {
            List<DivArgs> divArgsList = new List<DivArgs>{
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

            CollectionAssert.AreEqual(new long[] {3, 4, 3}, result.ConvertAll((divReply) => divReply.Quotient));
            CollectionAssert.AreEqual(new long[] {1, 16, 1}, result.ConvertAll((divReply) => divReply.Remainder));
        }

        [TestFixtureTearDown]
        public void Cleanup()
        {
            channel.Dispose();

            server.ShutdownAsync().Wait();
            GrpcEnvironment.Shutdown();
        }
    }
}

