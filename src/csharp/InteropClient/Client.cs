using System;
using System.Collections.Generic;
using NUnit.Framework;
using System.Text.RegularExpressions;
using Google.GRPC.Core;
using Google.GRPC.Core.Utils;
using Google.ProtocolBuffers;
using grpc.testing;

namespace Google.GRPC.Interop
{
    class Client
    {
        private class ClientOptions
        {
            public bool help;
            public string serverHost;
            public string serverHostOverride;
            public int? serverPort;
            public string testCase;
            public bool useTls;
            public bool useTestCa;
        }

        ClientOptions options;

        private Client(ClientOptions options)
        {
            this.options = options;
        }

        public static void Main(string[] args)
        {
            Console.WriteLine("gRPC C# interop testing client");
            ClientOptions options = ParseArguments(args);

            if (options.serverHost == null || !options.serverPort.HasValue || options.testCase == null)
            {
                Console.WriteLine("Missing required argument.");
                Console.WriteLine();
                options.help = true;
            }

            if (options.help)
            {
                Console.WriteLine("Usage:");
                Console.WriteLine("  --server_host=HOSTNAME");
                Console.WriteLine("  --server_host_override=HOSTNAME");
                Console.WriteLine("  --server_port=PORT");
                Console.WriteLine("  --test_case=TESTCASE");
                Console.WriteLine("  --use_tls=BOOLEAN");
                Console.WriteLine("  --use_test_ca=BOOLEAN");
                Console.WriteLine();  
                Environment.Exit(1);
            }

            var interopClient = new Client(options);
            interopClient.Run();
        }

        private void Run()
        {
            string addr = string.Format("{0}:{1}", options.serverHost, options.serverPort);
            using (Channel channel = new Channel(addr))
            {
                TestServiceGrpc.ITestServiceClient client = new TestServiceGrpc.TestServiceClientStub(channel);

                RunTestCase(options.testCase, client);
            }

            GrpcEnvironment.Shutdown();
        }

        private void RunTestCase(string testCase, TestServiceGrpc.ITestServiceClient client)
        {
            switch (testCase)
            {
                case "empty_unary":
                    RunEmptyUnary(client);
                    break;
                case "large_unary":
                    RunLargeUnary(client);
                    break;
                case "client_streaming":
                    RunClientStreaming(client);
                    break;
                case "server_streaming":
                    RunServerStreaming(client);
                    break;
                case "ping_pong":
                    RunPingPong(client);
                    break;
                case "empty_stream":
                    RunEmptyStream(client);
                    break;
                default:
                    throw new ArgumentException("Unknown test case " + testCase);
            }
        }

        private void RunEmptyUnary(TestServiceGrpc.ITestServiceClient client)
        {
            Console.WriteLine("running empty_unary");
            var response = client.EmptyCall(Empty.DefaultInstance);
            Assert.IsNotNull(response);
            Console.WriteLine("Passed!");
        }

        private void RunLargeUnary(TestServiceGrpc.ITestServiceClient client)
        {
            Console.WriteLine("running large_unary");
            var request = SimpleRequest.CreateBuilder()
                    .SetResponseType(PayloadType.COMPRESSABLE)
                    .SetResponseSize(314159)
                    .SetPayload(CreateZerosPayload(271828))
                    .Build();
             
            var response = client.UnaryCall(request);

            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(314159, response.Payload.Body.Length);
            Console.WriteLine("Passed!");
        }

        private void RunClientStreaming(TestServiceGrpc.ITestServiceClient client)
        {
            Console.WriteLine("running client_streaming");

            var bodySizes = new List<int>{27182, 8, 1828, 45904};

            var context = client.StreamingInputCall();
            foreach (var size in bodySizes)
            {
                context.Inputs.OnNext(
                    StreamingInputCallRequest.CreateBuilder().SetPayload(CreateZerosPayload(size)).Build());
            }
            context.Inputs.OnCompleted();

            var response = context.Task.Result;
            Assert.AreEqual(74922, response.AggregatedPayloadSize);
            Console.WriteLine("Passed!");
        }

        private void RunServerStreaming(TestServiceGrpc.ITestServiceClient client)
        {
            Console.WriteLine("running server_streaming");

            var bodySizes = new List<int>{31415, 9, 2653, 58979};

            var request = StreamingOutputCallRequest.CreateBuilder()
                .SetResponseType(PayloadType.COMPRESSABLE)
                .AddRangeResponseParameters(bodySizes.ConvertAll(
                        (size) => ResponseParameters.CreateBuilder().SetSize(size).Build()))
                .Build();

            var recorder = new RecordingObserver<StreamingOutputCallResponse>();
            client.StreamingOutputCall(request, recorder);

            var responseList = recorder.ToList().Result;

            foreach (var res in responseList)
            {
                Assert.AreEqual(PayloadType.COMPRESSABLE, res.Payload.Type);
            }
            CollectionAssert.AreEqual(bodySizes, responseList.ConvertAll((item) => item.Payload.Body.Length));
            Console.WriteLine("Passed!");
        }

        private void RunPingPong(TestServiceGrpc.ITestServiceClient client)
        {
            Console.WriteLine("running ping_pong");

            var recorder = new RecordingQueue<StreamingOutputCallResponse>();
            var inputs = client.FullDuplexCall(recorder);

            StreamingOutputCallResponse response;

            inputs.OnNext(StreamingOutputCallRequest.CreateBuilder()
                .SetResponseType(PayloadType.COMPRESSABLE)
                .AddResponseParameters(ResponseParameters.CreateBuilder().SetSize(31415))
                .SetPayload(CreateZerosPayload(27182)).Build());
           
            response = recorder.Queue.Take();             
            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(31415, response.Payload.Body.Length);

            inputs.OnNext(StreamingOutputCallRequest.CreateBuilder()
                          .SetResponseType(PayloadType.COMPRESSABLE)
                          .AddResponseParameters(ResponseParameters.CreateBuilder().SetSize(9))
                          .SetPayload(CreateZerosPayload(8)).Build());

            response = recorder.Queue.Take();             
            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(9, response.Payload.Body.Length);

            inputs.OnNext(StreamingOutputCallRequest.CreateBuilder()
                          .SetResponseType(PayloadType.COMPRESSABLE)
                          .AddResponseParameters(ResponseParameters.CreateBuilder().SetSize(2635))
                          .SetPayload(CreateZerosPayload(1828)).Build());

            response = recorder.Queue.Take();             
            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(2653, response.Payload.Body.Length);


            inputs.OnNext(StreamingOutputCallRequest.CreateBuilder()
                          .SetResponseType(PayloadType.COMPRESSABLE)
                          .AddResponseParameters(ResponseParameters.CreateBuilder().SetSize(58979))
                          .SetPayload(CreateZerosPayload(45904)).Build());

            response = recorder.Queue.Take();             
            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(58979, response.Payload.Body.Length);

            recorder.Finished.Wait();
            Assert.AreEqual(0, recorder.Queue.Count);

            Console.WriteLine("Passed!");
        }

        private void RunEmptyStream(TestServiceGrpc.ITestServiceClient client)
        {
            Console.WriteLine("running empty_stream");

            var recorder = new RecordingObserver<StreamingOutputCallResponse>();
            var inputs = client.FullDuplexCall(recorder);
            inputs.OnCompleted();

            var responseList = recorder.ToList().Result;
            Assert.AreEqual(0, responseList.Count);

            Console.WriteLine("Passed!");
        }


        private Payload CreateZerosPayload(int size) {
            return Payload.CreateBuilder().SetBody(ByteString.CopyFrom(new byte[size])).Build();
        }

        private static ClientOptions ParseArguments(string[] args)
        {
            var options = new ClientOptions();
            foreach(string arg in args)
            {
                ParseArgument(arg, options);
                if (options.help)
                {
                    break;
                }
            }
            return options;
        }

        private static void ParseArgument(string arg, ClientOptions options)
        {
            Match match;
            match = Regex.Match(arg, "--server_host=(.*)");
            if (match.Success)
            {
                options.serverHost = match.Groups[1].Value.Trim();
                return;
            }

            match = Regex.Match(arg, "--server_host_override=(.*)");
            if (match.Success)
            {
                options.serverHostOverride = match.Groups[1].Value.Trim();
                return;
            }

            match = Regex.Match(arg, "--server_port=(.*)");
            if (match.Success)
            {
                options.serverPort = int.Parse(match.Groups[1].Value.Trim());
                return;
            }

            match = Regex.Match(arg, "--test_case=(.*)");
            if (match.Success)
            {
                options.testCase = match.Groups[1].Value.Trim();
                return;
            }

            match = Regex.Match(arg, "--use_tls=(.*)");
            if (match.Success)
            {
                options.useTls = bool.Parse(match.Groups[1].Value.Trim());
                return;
            }

            match = Regex.Match(arg, "--use_test_ca=(.*)");
            if (match.Success)
            {
                options.useTestCa = bool.Parse(match.Groups[1].Value.Trim());
                return;
            }

            Console.WriteLine(string.Format("Unrecognized argument \"{0}\"", arg));
            options.help = true;
        }
    }
}
