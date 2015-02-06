using System;
using NUnit.Framework;
using System.Text.RegularExpressions;
using Google.GRPC.Core;
using Google.ProtocolBuffers;
using grpc.testing;

namespace InteropClient
{
    class InteropClient
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

        private InteropClient(ClientOptions options) {
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

            var interopClient = new InteropClient(options);
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
                default:
                    throw new ArgumentException("Unknown test case " + testCase);
            }
        }

        private void RunEmptyUnary(TestServiceGrpc.ITestServiceClient client)
        {
            Console.WriteLine("running empty_unary");
            var response = client.EmptyCall(Empty.DefaultInstance);
            Assert.IsNotNull(response);
        }

        private void RunLargeUnary(TestServiceGrpc.ITestServiceClient client)
        {
            Console.WriteLine("running large_unary");
            var request = SimpleRequest.CreateBuilder()
                    .SetResponseType(PayloadType.COMPRESSABLE)
                    .SetResponseSize(314159)
                    .SetPayload(Payload.CreateBuilder().SetBody(ByteString.CopyFrom(new byte[271828])))
                    .Build();
             
            var response = client.UnaryCall(request);

            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(314159, response.Payload.Body.Length);
            // TODO: assert that the response is all zeros...
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
