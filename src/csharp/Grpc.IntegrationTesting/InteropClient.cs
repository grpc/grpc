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
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

using Google.ProtocolBuffers;
using grpc.testing;
using Grpc.Auth;
using Grpc.Core;
using Grpc.Core.Utils;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class InteropClient
    {
        private const string ServiceAccountUser = "155450119199-3psnrh1sdr3d8cpj1v46naggf81mhdnk@developer.gserviceaccount.com";
        private const string ComputeEngineUser = "155450119199-r5aaqa2vqoa9g5mv2m6s3m1l293rlmel@developer.gserviceaccount.com";
        private const string AuthScope = "https://www.googleapis.com/auth/xapi.zoo";
        private const string AuthScopeResponse = "xapi.zoo";

        private class ClientOptions
        {
            public bool help;
            public string serverHost = "127.0.0.1";
            public string serverHostOverride = TestCredentials.DefaultHostOverride;
            public int? serverPort;
            public string testCase = "large_unary";
            public bool useTls;
            public bool useTestCa;
        }

        ClientOptions options;

        private InteropClient(ClientOptions options)
        {
            this.options = options;
        }

        public static void Run(string[] args)
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
            Credentials credentials = null;
            if (options.useTls)
            {
                credentials = TestCredentials.CreateTestClientCredentials(options.useTestCa);
            }

            List<ChannelOption> channelOptions = null;
            if (!string.IsNullOrEmpty(options.serverHostOverride))
            {
                channelOptions = new List<ChannelOption>
                {
                    new ChannelOption(ChannelOptions.SslTargetNameOverride, options.serverHostOverride)
                };
            }

            using (Channel channel = new Channel(options.serverHost, options.serverPort.Value, credentials, channelOptions))
            {
                TestService.TestServiceClient client = new TestService.TestServiceClient(channel);
                if (options.testCase == "service_account_creds" || options.testCase == "compute_engine_creds")
                {
                    var credential = GoogleCredential.GetApplicationDefault();
                    if (credential.IsCreateScopedRequired)
                    {
                        credential = credential.CreateScoped(new[] { AuthScope });
                    }
                    client.HeaderInterceptor = OAuth2Interceptors.FromCredential(credential);
                }

                RunTestCase(options.testCase, client);
            }
            GrpcEnvironment.Shutdown();
        }

        private void RunTestCase(string testCase, TestService.TestServiceClient client)
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
                case "service_account_creds":
                    RunServiceAccountCreds(client);
                    break;
                case "compute_engine_creds":
                    RunComputeEngineCreds(client);
                    break;
                case "oauth2_auth_token":
                    RunOAuth2AuthToken(client);
                    break;
                case "per_rpc_creds":
                    RunPerRpcCreds(client);
                    break;
                case "cancel_after_begin":
                    RunCancelAfterBegin(client);
                    break;
                case "cancel_after_first_response":
                    RunCancelAfterFirstResponse(client);
                    break;
                case "benchmark_empty_unary":
                    RunBenchmarkEmptyUnary(client);
                    break;
                default:
                    throw new ArgumentException("Unknown test case " + testCase);
            }
        }

        public static void RunEmptyUnary(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running empty_unary");
            var response = client.EmptyCall(Empty.DefaultInstance);
            Assert.IsNotNull(response);
            Console.WriteLine("Passed!");
        }

        public static void RunLargeUnary(TestService.ITestServiceClient client)
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

        public static void RunClientStreaming(TestService.ITestServiceClient client)
        {
            Task.Run(async () =>
            {
                Console.WriteLine("running client_streaming");

                var bodySizes = new List<int> { 27182, 8, 1828, 45904 }.ConvertAll((size) => StreamingInputCallRequest.CreateBuilder().SetPayload(CreateZerosPayload(size)).Build());

                using (var call = client.StreamingInputCall())
                {
                    await call.RequestStream.WriteAll(bodySizes);

                    var response = await call.ResponseAsync;
                    Assert.AreEqual(74922, response.AggregatedPayloadSize);
                }
                Console.WriteLine("Passed!");
            }).Wait();
        }

        public static void RunServerStreaming(TestService.ITestServiceClient client)
        {
            Task.Run(async () =>
            {
                Console.WriteLine("running server_streaming");

                var bodySizes = new List<int> { 31415, 9, 2653, 58979 };

                var request = StreamingOutputCallRequest.CreateBuilder()
                .SetResponseType(PayloadType.COMPRESSABLE)
                .AddRangeResponseParameters(bodySizes.ConvertAll(
                    (size) => ResponseParameters.CreateBuilder().SetSize(size).Build()))
                .Build();

                using (var call = client.StreamingOutputCall(request))
                {
                    var responseList = await call.ResponseStream.ToList();
                    foreach (var res in responseList)
                    {
                        Assert.AreEqual(PayloadType.COMPRESSABLE, res.Payload.Type);
                    }
                    CollectionAssert.AreEqual(bodySizes, responseList.ConvertAll((item) => item.Payload.Body.Length));
                }
                Console.WriteLine("Passed!");
            }).Wait();
        }

        public static void RunPingPong(TestService.ITestServiceClient client)
        {
            Task.Run(async () =>
            {
                Console.WriteLine("running ping_pong");

                using (var call = client.FullDuplexCall())
                {
                    await call.RequestStream.WriteAsync(StreamingOutputCallRequest.CreateBuilder()
                    .SetResponseType(PayloadType.COMPRESSABLE)
                    .AddResponseParameters(ResponseParameters.CreateBuilder().SetSize(31415))
                    .SetPayload(CreateZerosPayload(27182)).Build());

                    Assert.IsTrue(await call.ResponseStream.MoveNext());
                    Assert.AreEqual(PayloadType.COMPRESSABLE, call.ResponseStream.Current.Payload.Type);
                    Assert.AreEqual(31415, call.ResponseStream.Current.Payload.Body.Length);

                    await call.RequestStream.WriteAsync(StreamingOutputCallRequest.CreateBuilder()
                              .SetResponseType(PayloadType.COMPRESSABLE)
                              .AddResponseParameters(ResponseParameters.CreateBuilder().SetSize(9))
                              .SetPayload(CreateZerosPayload(8)).Build());

                    Assert.IsTrue(await call.ResponseStream.MoveNext());
                    Assert.AreEqual(PayloadType.COMPRESSABLE, call.ResponseStream.Current.Payload.Type);
                    Assert.AreEqual(9, call.ResponseStream.Current.Payload.Body.Length);

                    await call.RequestStream.WriteAsync(StreamingOutputCallRequest.CreateBuilder()
                              .SetResponseType(PayloadType.COMPRESSABLE)
                              .AddResponseParameters(ResponseParameters.CreateBuilder().SetSize(2653))
                              .SetPayload(CreateZerosPayload(1828)).Build());

                    Assert.IsTrue(await call.ResponseStream.MoveNext());
                    Assert.AreEqual(PayloadType.COMPRESSABLE, call.ResponseStream.Current.Payload.Type);
                    Assert.AreEqual(2653, call.ResponseStream.Current.Payload.Body.Length);

                    await call.RequestStream.WriteAsync(StreamingOutputCallRequest.CreateBuilder()
                              .SetResponseType(PayloadType.COMPRESSABLE)
                              .AddResponseParameters(ResponseParameters.CreateBuilder().SetSize(58979))
                              .SetPayload(CreateZerosPayload(45904)).Build());

                    Assert.IsTrue(await call.ResponseStream.MoveNext());
                    Assert.AreEqual(PayloadType.COMPRESSABLE, call.ResponseStream.Current.Payload.Type);
                    Assert.AreEqual(58979, call.ResponseStream.Current.Payload.Body.Length);

                    await call.RequestStream.CompleteAsync();

                    Assert.IsFalse(await call.ResponseStream.MoveNext());
                }
                Console.WriteLine("Passed!");
            }).Wait();
        }

        public static void RunEmptyStream(TestService.ITestServiceClient client)
        {
            Task.Run(async () =>
            {
                Console.WriteLine("running empty_stream");
                using (var call = client.FullDuplexCall())
                {
                    await call.RequestStream.CompleteAsync();

                    var responseList = await call.ResponseStream.ToList();
                    Assert.AreEqual(0, responseList.Count);
                }
                Console.WriteLine("Passed!");
            }).Wait();
        }

        public static void RunServiceAccountCreds(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running service_account_creds");
            var request = SimpleRequest.CreateBuilder()
                .SetResponseType(PayloadType.COMPRESSABLE)
                    .SetResponseSize(314159)
                    .SetPayload(CreateZerosPayload(271828))
                    .SetFillUsername(true)
                    .SetFillOauthScope(true)
                    .Build();

            var response = client.UnaryCall(request);

            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(314159, response.Payload.Body.Length);
            Assert.AreEqual(AuthScopeResponse, response.OauthScope);
            Assert.AreEqual(ServiceAccountUser, response.Username);
            Console.WriteLine("Passed!");
        }

        public static void RunComputeEngineCreds(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running compute_engine_creds");
            var request = SimpleRequest.CreateBuilder()
                .SetResponseType(PayloadType.COMPRESSABLE)
                    .SetResponseSize(314159)
                    .SetPayload(CreateZerosPayload(271828))
                    .SetFillUsername(true)
                    .SetFillOauthScope(true)
                    .Build();

            var response = client.UnaryCall(request);

            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(314159, response.Payload.Body.Length);
            Assert.AreEqual(AuthScopeResponse, response.OauthScope);
            Assert.AreEqual(ComputeEngineUser, response.Username);
            Console.WriteLine("Passed!");
        }

        public static void RunOAuth2AuthToken(TestService.TestServiceClient client)
        {
            Console.WriteLine("running oauth2_auth_token");
            var credential = GoogleCredential.GetApplicationDefault().CreateScoped(new[] { AuthScope });
            Assert.IsTrue(credential.RequestAccessTokenAsync(CancellationToken.None).Result);
            string oauth2Token = credential.Token.AccessToken;

            client.HeaderInterceptor = OAuth2Interceptors.FromAccessToken(oauth2Token);

            var request = SimpleRequest.CreateBuilder()
                .SetFillUsername(true)
                .SetFillOauthScope(true)
                .Build();

            var response = client.UnaryCall(request);

            Assert.AreEqual(AuthScopeResponse, response.OauthScope);
            Assert.AreEqual(ServiceAccountUser, response.Username);
            Console.WriteLine("Passed!");
        }

        public static void RunPerRpcCreds(TestService.TestServiceClient client)
        {
            Console.WriteLine("running per_rpc_creds");

            var credential = GoogleCredential.GetApplicationDefault().CreateScoped(new[] { AuthScope });
            Assert.IsTrue(credential.RequestAccessTokenAsync(CancellationToken.None).Result);
            string oauth2Token = credential.Token.AccessToken;
            var headerInterceptor = OAuth2Interceptors.FromAccessToken(oauth2Token);

            var request = SimpleRequest.CreateBuilder()
                .SetFillUsername(true)
                .SetFillOauthScope(true)
                .Build();

            var headers = new Metadata();
            headerInterceptor(headers);
            var response = client.UnaryCall(request, headers: headers);

            Assert.AreEqual(AuthScopeResponse, response.OauthScope);
            Assert.AreEqual(ServiceAccountUser, response.Username);
            Console.WriteLine("Passed!");
        }

        public static void RunCancelAfterBegin(TestService.ITestServiceClient client)
        {
            Task.Run(async () =>
            {
                Console.WriteLine("running cancel_after_begin");

                var cts = new CancellationTokenSource();
                using (var call = client.StreamingInputCall(cancellationToken: cts.Token))
                {
                    // TODO(jtattermusch): we need this to ensure call has been initiated once we cancel it.
                    await Task.Delay(1000);
                    cts.Cancel();

                    try
                    {
                        var response = await call.ResponseAsync;
                        Assert.Fail();
                    }
                    catch (RpcException e)
                    {
                        Assert.AreEqual(StatusCode.Cancelled, e.Status.StatusCode);
                    }
                }
                Console.WriteLine("Passed!");
            }).Wait();
        }

        public static void RunCancelAfterFirstResponse(TestService.ITestServiceClient client)
        {
            Task.Run(async () =>
            {
                Console.WriteLine("running cancel_after_first_response");

                var cts = new CancellationTokenSource();
                using (var call = client.FullDuplexCall(cancellationToken: cts.Token))
                {
                    await call.RequestStream.WriteAsync(StreamingOutputCallRequest.CreateBuilder()
                        .SetResponseType(PayloadType.COMPRESSABLE)
                        .AddResponseParameters(ResponseParameters.CreateBuilder().SetSize(31415))
                        .SetPayload(CreateZerosPayload(27182)).Build());

                    Assert.IsTrue(await call.ResponseStream.MoveNext());
                    Assert.AreEqual(PayloadType.COMPRESSABLE, call.ResponseStream.Current.Payload.Type);
                    Assert.AreEqual(31415, call.ResponseStream.Current.Payload.Body.Length);

                    cts.Cancel();

                    try
                    {
                        await call.ResponseStream.MoveNext();
                        Assert.Fail();
                    }
                    catch (RpcException e)
                    {
                        Assert.AreEqual(StatusCode.Cancelled, e.Status.StatusCode);
                    }
                }
                Console.WriteLine("Passed!");
            }).Wait();
        }

        // This is not an official interop test, but it's useful.
        public static void RunBenchmarkEmptyUnary(TestService.ITestServiceClient client)
        {
            BenchmarkUtil.RunBenchmark(10000, 10000,
                                       () => { client.EmptyCall(Empty.DefaultInstance); });
        }

        private static Payload CreateZerosPayload(int size)
        {
            return Payload.CreateBuilder().SetBody(ByteString.CopyFrom(new byte[size])).Build();
        }

        private static ClientOptions ParseArguments(string[] args)
        {
            var options = new ClientOptions();
            foreach (string arg in args)
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
