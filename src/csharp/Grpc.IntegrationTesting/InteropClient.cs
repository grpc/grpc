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

using Google.Apis.Auth.OAuth2;
using Google.Protobuf;
using Grpc.Auth;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
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
            interopClient.Run().Wait();
        }

        private async Task Run()
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

            var channel = new Channel(options.serverHost, options.serverPort.Value, credentials, channelOptions);
            TestService.TestServiceClient client = new TestService.TestServiceClient(channel);
            await RunTestCaseAsync(options.testCase, client);
            channel.ShutdownAsync().Wait();
        }

        private async Task RunTestCaseAsync(string testCase, TestService.TestServiceClient client)
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
                    await RunClientStreamingAsync(client);
                    break;
                case "server_streaming":
                    await RunServerStreamingAsync(client);
                    break;
                case "ping_pong":
                    await RunPingPongAsync(client);
                    break;
                case "empty_stream":
                    await RunEmptyStreamAsync(client);
                    break;
                case "service_account_creds":
                    await RunServiceAccountCredsAsync(client);
                    break;
                case "compute_engine_creds":
                    await RunComputeEngineCredsAsync(client);
                    break;
                case "jwt_token_creds":
                    await RunJwtTokenCredsAsync(client);
                    break;
                case "oauth2_auth_token":
                    await RunOAuth2AuthTokenAsync(client);
                    break;
                case "per_rpc_creds":
                    await RunPerRpcCredsAsync(client);
                    break;
                case "cancel_after_begin":
                    await RunCancelAfterBeginAsync(client);
                    break;
                case "cancel_after_first_response":
                    await RunCancelAfterFirstResponseAsync(client);
                    break;
                case "timeout_on_sleeping_server":
                    await RunTimeoutOnSleepingServerAsync(client);
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
            var response = client.EmptyCall(new Empty());
            Assert.IsNotNull(response);
            Console.WriteLine("Passed!");
        }

        public static void RunLargeUnary(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running large_unary");
            var request = new SimpleRequest
            {
                ResponseType = PayloadType.COMPRESSABLE,
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828)
            };

            var response = client.UnaryCall(request);

            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(314159, response.Payload.Body.Length);
            Console.WriteLine("Passed!");
        }

        public static async Task RunClientStreamingAsync(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running client_streaming");

            var bodySizes = new List<int> { 27182, 8, 1828, 45904 }.ConvertAll((size) => new StreamingInputCallRequest { Payload = CreateZerosPayload(size) });

            using (var call = client.StreamingInputCall())
            {
                await call.RequestStream.WriteAllAsync(bodySizes);

                var response = await call.ResponseAsync;
                Assert.AreEqual(74922, response.AggregatedPayloadSize);
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunServerStreamingAsync(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running server_streaming");

            var bodySizes = new List<int> { 31415, 9, 2653, 58979 };

            var request = new StreamingOutputCallRequest
            {
                ResponseType = PayloadType.COMPRESSABLE,
                ResponseParameters = { bodySizes.ConvertAll((size) => new ResponseParameters { Size = size }) }
            };

            using (var call = client.StreamingOutputCall(request))
            {
                var responseList = await call.ResponseStream.ToListAsync();
                foreach (var res in responseList)
                {
                    Assert.AreEqual(PayloadType.COMPRESSABLE, res.Payload.Type);
                }
                CollectionAssert.AreEqual(bodySizes, responseList.ConvertAll((item) => item.Payload.Body.Length));
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunPingPongAsync(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running ping_pong");

            using (var call = client.FullDuplexCall())
            {
                await call.RequestStream.WriteAsync(new StreamingOutputCallRequest
                {
                    ResponseType = PayloadType.COMPRESSABLE,
                    ResponseParameters = { new ResponseParameters { Size = 31415 } },
                    Payload = CreateZerosPayload(27182)
                });

                Assert.IsTrue(await call.ResponseStream.MoveNext());
                Assert.AreEqual(PayloadType.COMPRESSABLE, call.ResponseStream.Current.Payload.Type);
                Assert.AreEqual(31415, call.ResponseStream.Current.Payload.Body.Length);

                await call.RequestStream.WriteAsync(new StreamingOutputCallRequest
                {
                    ResponseType = PayloadType.COMPRESSABLE,
                    ResponseParameters = { new ResponseParameters { Size = 9 } },
                    Payload = CreateZerosPayload(8)
                });

                Assert.IsTrue(await call.ResponseStream.MoveNext());
                Assert.AreEqual(PayloadType.COMPRESSABLE, call.ResponseStream.Current.Payload.Type);
                Assert.AreEqual(9, call.ResponseStream.Current.Payload.Body.Length);

                await call.RequestStream.WriteAsync(new StreamingOutputCallRequest
                {
                    ResponseType = PayloadType.COMPRESSABLE,
                    ResponseParameters = { new ResponseParameters { Size = 2653 } },
                    Payload = CreateZerosPayload(1828)
                });

                Assert.IsTrue(await call.ResponseStream.MoveNext());
                Assert.AreEqual(PayloadType.COMPRESSABLE, call.ResponseStream.Current.Payload.Type);
                Assert.AreEqual(2653, call.ResponseStream.Current.Payload.Body.Length);

                await call.RequestStream.WriteAsync(new StreamingOutputCallRequest
                {
                    ResponseType = PayloadType.COMPRESSABLE,
                    ResponseParameters = { new ResponseParameters { Size = 58979 } },
                    Payload = CreateZerosPayload(45904)
                });

                Assert.IsTrue(await call.ResponseStream.MoveNext());
                Assert.AreEqual(PayloadType.COMPRESSABLE, call.ResponseStream.Current.Payload.Type);
                Assert.AreEqual(58979, call.ResponseStream.Current.Payload.Body.Length);

                await call.RequestStream.CompleteAsync();

                Assert.IsFalse(await call.ResponseStream.MoveNext());
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunEmptyStreamAsync(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running empty_stream");
            using (var call = client.FullDuplexCall())
            {
                await call.RequestStream.CompleteAsync();

                var responseList = await call.ResponseStream.ToListAsync();
                Assert.AreEqual(0, responseList.Count);
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunServiceAccountCredsAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running service_account_creds");
            var credential = await GoogleCredential.GetApplicationDefaultAsync();
            credential = credential.CreateScoped(new[] { AuthScope });
            client.HeaderInterceptor = AuthInterceptors.FromCredential(credential);

            var request = new SimpleRequest
            {
                ResponseType = PayloadType.COMPRESSABLE,
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828),
                FillUsername = true,
                FillOauthScope = true
            };

            var response = client.UnaryCall(request);

            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(314159, response.Payload.Body.Length);
            Assert.AreEqual(AuthScopeResponse, response.OauthScope);
            Assert.AreEqual(ServiceAccountUser, response.Username);
            Console.WriteLine("Passed!");
        }

        public static async Task RunComputeEngineCredsAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running compute_engine_creds");
            var credential = await GoogleCredential.GetApplicationDefaultAsync();
            Assert.IsFalse(credential.IsCreateScopedRequired);
            client.HeaderInterceptor = AuthInterceptors.FromCredential(credential);
            
            var request = new SimpleRequest
            {
                ResponseType = PayloadType.COMPRESSABLE,
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828),
                FillUsername = true,
                FillOauthScope = true
            };

            var response = client.UnaryCall(request);

            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(314159, response.Payload.Body.Length);
            Assert.AreEqual(AuthScopeResponse, response.OauthScope);
            Assert.AreEqual(ComputeEngineUser, response.Username);
            Console.WriteLine("Passed!");
        }

        public static async Task RunJwtTokenCredsAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running jwt_token_creds");
            var credential = await GoogleCredential.GetApplicationDefaultAsync();
            // check this a credential with scope support, but don't add the scope.
            Assert.IsTrue(credential.IsCreateScopedRequired);
            client.HeaderInterceptor = AuthInterceptors.FromCredential(credential);

            var request = new SimpleRequest
            {
                ResponseType = PayloadType.COMPRESSABLE,
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828),
                FillUsername = true,
                FillOauthScope = true
            };

            var response = client.UnaryCall(request);

            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(314159, response.Payload.Body.Length);
            Assert.AreEqual(ServiceAccountUser, response.Username);
            Console.WriteLine("Passed!");
        }

        public static async Task RunOAuth2AuthTokenAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running oauth2_auth_token");
            ITokenAccess credential = (await GoogleCredential.GetApplicationDefaultAsync()).CreateScoped(new[] { AuthScope });
            string oauth2Token = await credential.GetAccessTokenForRequestAsync();

            client.HeaderInterceptor = AuthInterceptors.FromAccessToken(oauth2Token);

            var request = new SimpleRequest
            {
                FillUsername = true,
                FillOauthScope = true
            };

            var response = client.UnaryCall(request);

            Assert.AreEqual(AuthScopeResponse, response.OauthScope);
            Assert.AreEqual(ServiceAccountUser, response.Username);
            Console.WriteLine("Passed!");
        }

        public static async Task RunPerRpcCredsAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running per_rpc_creds");

            ITokenAccess credential = (await GoogleCredential.GetApplicationDefaultAsync()).CreateScoped(new[] { AuthScope });
            string oauth2Token = await credential.GetAccessTokenForRequestAsync();
            var headerInterceptor = AuthInterceptors.FromAccessToken(oauth2Token);

            var request = new SimpleRequest
            {
                FillUsername = true,
                FillOauthScope = true
            };

            var headers = new Metadata();
            headerInterceptor(null, "", headers);
            var response = client.UnaryCall(request, headers: headers);

            Assert.AreEqual(AuthScopeResponse, response.OauthScope);
            Assert.AreEqual(ServiceAccountUser, response.Username);
            Console.WriteLine("Passed!");
        }

        public static async Task RunCancelAfterBeginAsync(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running cancel_after_begin");

            var cts = new CancellationTokenSource();
            using (var call = client.StreamingInputCall(cancellationToken: cts.Token))
            {
                // TODO(jtattermusch): we need this to ensure call has been initiated once we cancel it.
                await Task.Delay(1000);
                cts.Cancel();

                var ex = Assert.Throws<RpcException>(async () => await call.ResponseAsync);
                Assert.AreEqual(StatusCode.Cancelled, ex.Status.StatusCode);
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunCancelAfterFirstResponseAsync(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running cancel_after_first_response");

            var cts = new CancellationTokenSource();
            using (var call = client.FullDuplexCall(cancellationToken: cts.Token))
            {
                await call.RequestStream.WriteAsync(new StreamingOutputCallRequest
                {
                    ResponseType = PayloadType.COMPRESSABLE,
                    ResponseParameters = { new ResponseParameters { Size = 31415 } },
                    Payload = CreateZerosPayload(27182)
                });

                Assert.IsTrue(await call.ResponseStream.MoveNext());
                Assert.AreEqual(PayloadType.COMPRESSABLE, call.ResponseStream.Current.Payload.Type);
                Assert.AreEqual(31415, call.ResponseStream.Current.Payload.Body.Length);

                cts.Cancel();

                var ex = Assert.Throws<RpcException>(async () => await call.ResponseStream.MoveNext());
                Assert.AreEqual(StatusCode.Cancelled, ex.Status.StatusCode);
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunTimeoutOnSleepingServerAsync(TestService.ITestServiceClient client)
        {
            Console.WriteLine("running timeout_on_sleeping_server");

            var deadline = DateTime.UtcNow.AddMilliseconds(1);
            using (var call = client.FullDuplexCall(deadline: deadline))
            {
                try
                {
                    await call.RequestStream.WriteAsync(new StreamingOutputCallRequest { Payload = CreateZerosPayload(27182) });
                }
                catch (InvalidOperationException)
                {
                    // Deadline was reached before write has started. Eat the exception and continue.
                }

                var ex = Assert.Throws<RpcException>(async () => await call.ResponseStream.MoveNext());
                Assert.AreEqual(StatusCode.DeadlineExceeded, ex.Status.StatusCode);
            }
            Console.WriteLine("Passed!");
        }

        // This is not an official interop test, but it's useful.
        public static void RunBenchmarkEmptyUnary(TestService.ITestServiceClient client)
        {
            BenchmarkUtil.RunBenchmark(10000, 10000,
                                       () => { client.EmptyCall(new Empty()); });
        }

        private static Payload CreateZerosPayload(int size)
        {
            return new Payload { Body = ByteString.CopyFrom(new byte[size]) };
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
