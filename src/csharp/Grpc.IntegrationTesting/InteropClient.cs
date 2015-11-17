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
using System.IO;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

using CommandLine;
using CommandLine.Text;
using Google.Apis.Auth.OAuth2;
using Google.Protobuf;
using Grpc.Auth;
using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Testing;
using Newtonsoft.Json.Linq;
using NUnit.Framework;

namespace Grpc.IntegrationTesting
{
    public class InteropClient
    {
        private class ClientOptions
        {
            [Option("server_host", DefaultValue = "127.0.0.1")]
            public string ServerHost { get; set; }

            [Option("server_host_override", DefaultValue = TestCredentials.DefaultHostOverride)]
            public string ServerHostOverride { get; set; }

            [Option("server_port", Required = true)]
            public int ServerPort { get; set; }

            [Option("test_case", DefaultValue = "large_unary")]
            public string TestCase { get; set; }

            // Deliberately using nullable bool type to allow --use_tls=true syntax (as opposed to --use_tls)
            [Option("use_tls", DefaultValue = false)]
            public bool? UseTls { get; set; }

            // Deliberately using nullable bool type to allow --use_test_ca=true syntax (as opposed to --use_test_ca)
            [Option("use_test_ca", DefaultValue = false)]
            public bool? UseTestCa { get; set; }

            [Option("default_service_account", Required = false)]
            public string DefaultServiceAccount { get; set; }

            [Option("oauth_scope", Required = false)]
            public string OAuthScope { get; set; }

            [Option("service_account_key_file", Required = false)]
            public string ServiceAccountKeyFile { get; set; }

            [HelpOption]
            public string GetUsage()
            {
                var help = new HelpText
                {
                    Heading = "gRPC C# interop testing client",
                    AddDashesToOption = true
                };
                help.AddPreOptionsLine("Usage:");
                help.AddOptions(this);
                return help;
            }
        }

        ClientOptions options;

        private InteropClient(ClientOptions options)
        {
            this.options = options;
        }

        public static void Run(string[] args)
        {
            var options = new ClientOptions();
            if (!Parser.Default.ParseArguments(args, options))
            {
                Environment.Exit(1);
            }

            var interopClient = new InteropClient(options);
            interopClient.Run().Wait();
        }

        private async Task Run()
        {
            var credentials = await CreateCredentialsAsync();
            
            List<ChannelOption> channelOptions = null;
            if (!string.IsNullOrEmpty(options.ServerHostOverride))
            {
                channelOptions = new List<ChannelOption>
                {
                    new ChannelOption(ChannelOptions.SslTargetNameOverride, options.ServerHostOverride)
                };
            }
            var channel = new Channel(options.ServerHost, options.ServerPort, credentials, channelOptions);
            TestService.TestServiceClient client = new TestService.TestServiceClient(channel);
            await RunTestCaseAsync(client, options);
            await channel.ShutdownAsync();
        }

        private async Task<ChannelCredentials> CreateCredentialsAsync()
        {
            var credentials = ChannelCredentials.Insecure;
            if (options.UseTls.Value)
            {
                credentials = options.UseTestCa.Value ? TestCredentials.CreateSslCredentials() : new SslCredentials();
            }

            if (options.TestCase == "jwt_token_creds")
            {
                var googleCredential = await GoogleCredential.GetApplicationDefaultAsync();
                Assert.IsTrue(googleCredential.IsCreateScopedRequired);
                credentials = ChannelCredentials.Create(credentials, googleCredential.ToCallCredentials());
            }

            if (options.TestCase == "compute_engine_creds")
            {
                var googleCredential = await GoogleCredential.GetApplicationDefaultAsync();
                Assert.IsFalse(googleCredential.IsCreateScopedRequired);
                credentials = ChannelCredentials.Create(credentials, googleCredential.ToCallCredentials());
            }
            return credentials;
        }

        private async Task RunTestCaseAsync(TestService.TestServiceClient client, ClientOptions options)
        {
            switch (options.TestCase)
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
                case "compute_engine_creds":
                    RunComputeEngineCreds(client, options.DefaultServiceAccount, options.OAuthScope);
                    break;
                case "jwt_token_creds":
                    RunJwtTokenCreds(client);
                    break;
                case "oauth2_auth_token":
                    await RunOAuth2AuthTokenAsync(client, options.OAuthScope);
                    break;
                case "per_rpc_creds":
                    await RunPerRpcCredsAsync(client, options.OAuthScope);
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
                    throw new ArgumentException("Unknown test case " + options.TestCase);
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

        public static void RunComputeEngineCreds(TestService.TestServiceClient client, string defaultServiceAccount, string oauthScope)
        {
            Console.WriteLine("running compute_engine_creds");

            var request = new SimpleRequest
            {
                ResponseType = PayloadType.COMPRESSABLE,
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828),
                FillUsername = true,
                FillOauthScope = true
            };

            // not setting credentials here because they were set on channel already
            var response = client.UnaryCall(request);

            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(314159, response.Payload.Body.Length);
            Assert.False(string.IsNullOrEmpty(response.OauthScope));
            Assert.True(oauthScope.Contains(response.OauthScope));
            Assert.AreEqual(defaultServiceAccount, response.Username);
            Console.WriteLine("Passed!");
        }

        public static void RunJwtTokenCreds(TestService.TestServiceClient client)
        {
            Console.WriteLine("running jwt_token_creds");
           
            var request = new SimpleRequest
            {
                ResponseType = PayloadType.COMPRESSABLE,
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828),
                FillUsername = true,
            };

            // not setting credentials here because they were set on channel already
            var response = client.UnaryCall(request);

            Assert.AreEqual(PayloadType.COMPRESSABLE, response.Payload.Type);
            Assert.AreEqual(314159, response.Payload.Body.Length);
            Assert.AreEqual(GetEmailFromServiceAccountFile(), response.Username);
            Console.WriteLine("Passed!");
        }

        public static async Task RunOAuth2AuthTokenAsync(TestService.TestServiceClient client, string oauthScope)
        {
            Console.WriteLine("running oauth2_auth_token");
            ITokenAccess credential = (await GoogleCredential.GetApplicationDefaultAsync()).CreateScoped(new[] { oauthScope });
            string oauth2Token = await credential.GetAccessTokenForRequestAsync();

            var credentials = GoogleGrpcCredentials.FromAccessToken(oauth2Token);
            var request = new SimpleRequest
            {
                FillUsername = true,
                FillOauthScope = true
            };

            var response = client.UnaryCall(request, new CallOptions(credentials: credentials));

            Assert.False(string.IsNullOrEmpty(response.OauthScope));
            Assert.True(oauthScope.Contains(response.OauthScope));
            Assert.AreEqual(GetEmailFromServiceAccountFile(), response.Username);
            Console.WriteLine("Passed!");
        }

        public static async Task RunPerRpcCredsAsync(TestService.TestServiceClient client, string oauthScope)
        {
            Console.WriteLine("running per_rpc_creds");
            ITokenAccess googleCredential = await GoogleCredential.GetApplicationDefaultAsync();

            var credentials = googleCredential.ToCallCredentials();
            var request = new SimpleRequest
            {
                FillUsername = true,
            };

            var response = client.UnaryCall(request, new CallOptions(credentials: credentials));

            Assert.AreEqual(GetEmailFromServiceAccountFile(), response.Username);
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

        // extracts the client_email field from service account file used for auth test cases
        private static string GetEmailFromServiceAccountFile()
        {
            string keyFile = Environment.GetEnvironmentVariable("GOOGLE_APPLICATION_CREDENTIALS");
            Assert.IsNotNull(keyFile);

            var jobject = JObject.Parse(File.ReadAllText(keyFile));
            string email = jobject.GetValue("client_email").Value<string>();
            Assert.IsTrue(email.Length > 0);  // spec requires nonempty client email.
            return email;
        }
    }
}
