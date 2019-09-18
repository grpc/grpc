#region Copyright notice and license

// Copyright 2015-2016 gRPC authors.
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
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

using CommandLine;
using CommandLine.Text;
using Google.Apis.Auth.OAuth2;
using Google.Protobuf;
using Grpc.Auth;
using Grpc.Core;
using Grpc.Core.Logging;
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
            [Option("server_host", Default = "localhost")]
            public string ServerHost { get; set; }

            [Option("server_host_override")]
            public string ServerHostOverride { get; set; }

            [Option("server_port", Required = true)]
            public int ServerPort { get; set; }

            [Option("test_case", Default = "large_unary")]
            public string TestCase { get; set; }

            // Deliberately using nullable bool type to allow --use_tls=true syntax (as opposed to --use_tls)
            [Option("use_tls", Default = false)]
            public bool? UseTls { get; set; }

            // Deliberately using nullable bool type to allow --use_test_ca=true syntax (as opposed to --use_test_ca)
            [Option("use_test_ca", Default = false)]
            public bool? UseTestCa { get; set; }

            [Option("default_service_account", Required = false)]
            public string DefaultServiceAccount { get; set; }

            [Option("oauth_scope", Required = false)]
            public string OAuthScope { get; set; }

            [Option("service_account_key_file", Required = false)]
            public string ServiceAccountKeyFile { get; set; }
        }

        ClientOptions options;

        private InteropClient(ClientOptions options)
        {
            this.options = options;
        }

        public static void Run(string[] args)
        {
            GrpcEnvironment.SetLogger(new ConsoleLogger());
            var parserResult = Parser.Default.ParseArguments<ClientOptions>(args)
                .WithNotParsed(errors => Environment.Exit(1))
                .WithParsed(options =>
                {
                    var interopClient = new InteropClient(options);
                    interopClient.Run().Wait();
                });
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
            await RunTestCaseAsync(channel, options);
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

        private async Task RunTestCaseAsync(Channel channel, ClientOptions options)
        {
            var client = new TestService.TestServiceClient(channel);
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
                case "custom_metadata":
                    await RunCustomMetadataAsync(client);
                    break;
                case "status_code_and_message":
                    await RunStatusCodeAndMessageAsync(client);
                    break;
                case "unimplemented_service":
                    RunUnimplementedService(new UnimplementedService.UnimplementedServiceClient(channel));
                    break;
                case "special_status_message":
                    await RunSpecialStatusMessageAsync(client);
                    break;
                case "unimplemented_method":
                    RunUnimplementedMethod(client);
                    break;
                case "client_compressed_unary":
                    RunClientCompressedUnary(client);
                    break;
                case "client_compressed_streaming":
                    await RunClientCompressedStreamingAsync(client);
                    break;
                default:
                    throw new ArgumentException("Unknown test case " + options.TestCase);
            }
        }

        public static void RunEmptyUnary(TestService.TestServiceClient client)
        {
            Console.WriteLine("running empty_unary");
            var response = client.EmptyCall(new Empty());
            Assert.IsNotNull(response);
            Console.WriteLine("Passed!");
        }

        public static void RunLargeUnary(TestService.TestServiceClient client)
        {
            Console.WriteLine("running large_unary");
            var request = new SimpleRequest
            {
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828)
            };
            var response = client.UnaryCall(request);

            Assert.AreEqual(314159, response.Payload.Body.Length);
            Console.WriteLine("Passed!");
        }

        public static async Task RunClientStreamingAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running client_streaming");

            var bodySizes = new List<int> { 27182, 8, 1828, 45904 }.Select((size) => new StreamingInputCallRequest { Payload = CreateZerosPayload(size) });

            using (var call = client.StreamingInputCall())
            {
                await call.RequestStream.WriteAllAsync(bodySizes);

                var response = await call.ResponseAsync;
                Assert.AreEqual(74922, response.AggregatedPayloadSize);
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunServerStreamingAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running server_streaming");

            var bodySizes = new List<int> { 31415, 9, 2653, 58979 };

            var request = new StreamingOutputCallRequest
            {
                ResponseParameters = { bodySizes.Select((size) => new ResponseParameters { Size = size }) }
            };

            using (var call = client.StreamingOutputCall(request))
            {
                var responseList = await call.ResponseStream.ToListAsync();
                CollectionAssert.AreEqual(bodySizes, responseList.Select((item) => item.Payload.Body.Length));
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunPingPongAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running ping_pong");

            using (var call = client.FullDuplexCall())
            {
                await call.RequestStream.WriteAsync(new StreamingOutputCallRequest
                {
                    ResponseParameters = { new ResponseParameters { Size = 31415 } },
                    Payload = CreateZerosPayload(27182)
                });

                Assert.IsTrue(await call.ResponseStream.MoveNext());
                Assert.AreEqual(31415, call.ResponseStream.Current.Payload.Body.Length);

                await call.RequestStream.WriteAsync(new StreamingOutputCallRequest
                {
                    ResponseParameters = { new ResponseParameters { Size = 9 } },
                    Payload = CreateZerosPayload(8)
                });

                Assert.IsTrue(await call.ResponseStream.MoveNext());
                Assert.AreEqual(9, call.ResponseStream.Current.Payload.Body.Length);

                await call.RequestStream.WriteAsync(new StreamingOutputCallRequest
                {
                    ResponseParameters = { new ResponseParameters { Size = 2653 } },
                    Payload = CreateZerosPayload(1828)
                });

                Assert.IsTrue(await call.ResponseStream.MoveNext());
                Assert.AreEqual(2653, call.ResponseStream.Current.Payload.Body.Length);

                await call.RequestStream.WriteAsync(new StreamingOutputCallRequest
                {
                    ResponseParameters = { new ResponseParameters { Size = 58979 } },
                    Payload = CreateZerosPayload(45904)
                });

                Assert.IsTrue(await call.ResponseStream.MoveNext());
                Assert.AreEqual(58979, call.ResponseStream.Current.Payload.Body.Length);

                await call.RequestStream.CompleteAsync();

                Assert.IsFalse(await call.ResponseStream.MoveNext());
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunEmptyStreamAsync(TestService.TestServiceClient client)
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
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828),
                FillUsername = true,
                FillOauthScope = true
            };

            // not setting credentials here because they were set on channel already
            var response = client.UnaryCall(request);

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
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828),
                FillUsername = true,
            };

            // not setting credentials here because they were set on channel already
            var response = client.UnaryCall(request);

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

        public static async Task RunCancelAfterBeginAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running cancel_after_begin");

            var cts = new CancellationTokenSource();
            using (var call = client.StreamingInputCall(cancellationToken: cts.Token))
            {
                // TODO(jtattermusch): we need this to ensure call has been initiated once we cancel it.
                await Task.Delay(1000);
                cts.Cancel();

                var ex = Assert.ThrowsAsync<RpcException>(async () => await call.ResponseAsync);
                Assert.AreEqual(StatusCode.Cancelled, ex.Status.StatusCode);
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunCancelAfterFirstResponseAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running cancel_after_first_response");

            var cts = new CancellationTokenSource();
            using (var call = client.FullDuplexCall(cancellationToken: cts.Token))
            {
                await call.RequestStream.WriteAsync(new StreamingOutputCallRequest
                {
                    ResponseParameters = { new ResponseParameters { Size = 31415 } },
                    Payload = CreateZerosPayload(27182)
                });

                Assert.IsTrue(await call.ResponseStream.MoveNext());
                Assert.AreEqual(31415, call.ResponseStream.Current.Payload.Body.Length);

                cts.Cancel();

                try
                {
                    // cannot use Assert.ThrowsAsync because it uses Task.Wait and would deadlock.
                    await call.ResponseStream.MoveNext();
                    Assert.Fail();
                }
                catch (RpcException ex)
                {
                    Assert.AreEqual(StatusCode.Cancelled, ex.Status.StatusCode);
                }
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunTimeoutOnSleepingServerAsync(TestService.TestServiceClient client)
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
                catch (RpcException)
                {
                    // Deadline was reached before write has started. Eat the exception and continue.
                }

                try
                {
                    await call.ResponseStream.MoveNext();
                    Assert.Fail();
                }
                catch (RpcException ex)
                {
                    Assert.AreEqual(StatusCode.DeadlineExceeded, ex.Status.StatusCode);
                }
            }
            Console.WriteLine("Passed!");
        }

        public static async Task RunCustomMetadataAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running custom_metadata");
            {
                // step 1: test unary call
                var request = new SimpleRequest
                {
                    ResponseSize = 314159,
                    Payload = CreateZerosPayload(271828)
                };

                var call = client.UnaryCallAsync(request, headers: CreateTestMetadata());
                await call.ResponseAsync;

                var responseHeaders = await call.ResponseHeadersAsync;
                var responseTrailers = call.GetTrailers();

                Assert.AreEqual("test_initial_metadata_value", responseHeaders.First((entry) => entry.Key == "x-grpc-test-echo-initial").Value);
                CollectionAssert.AreEqual(new byte[] { 0xab, 0xab, 0xab }, responseTrailers.First((entry) => entry.Key == "x-grpc-test-echo-trailing-bin").ValueBytes);
            }

            {
                // step 2: test full duplex call
                var request = new StreamingOutputCallRequest
                {
                    ResponseParameters = { new ResponseParameters { Size = 31415 } },
                    Payload = CreateZerosPayload(27182)
                };

                var call = client.FullDuplexCall(headers: CreateTestMetadata());

                await call.RequestStream.WriteAsync(request);
                await call.RequestStream.CompleteAsync();
                await call.ResponseStream.ToListAsync();

                var responseHeaders = await call.ResponseHeadersAsync;
                var responseTrailers = call.GetTrailers();

                Assert.AreEqual("test_initial_metadata_value", responseHeaders.First((entry) => entry.Key == "x-grpc-test-echo-initial").Value);
                CollectionAssert.AreEqual(new byte[] { 0xab, 0xab, 0xab }, responseTrailers.First((entry) => entry.Key == "x-grpc-test-echo-trailing-bin").ValueBytes);
            }

            Console.WriteLine("Passed!");
        }

        public static async Task RunStatusCodeAndMessageAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running status_code_and_message");
            var echoStatus = new EchoStatus
            {
                Code = 2,
                Message = "test status message"
            };

            {
                // step 1: test unary call
                var request = new SimpleRequest { ResponseStatus = echoStatus };

                var e = Assert.Throws<RpcException>(() => client.UnaryCall(request));
                Assert.AreEqual(StatusCode.Unknown, e.Status.StatusCode);
                Assert.AreEqual(echoStatus.Message, e.Status.Detail);
            }

            {
                // step 2: test full duplex call
                var request = new StreamingOutputCallRequest { ResponseStatus = echoStatus };

                var call = client.FullDuplexCall();
                await call.RequestStream.WriteAsync(request);
                await call.RequestStream.CompleteAsync();

                try
                {
                    // cannot use Assert.ThrowsAsync because it uses Task.Wait and would deadlock.
                    await call.ResponseStream.ToListAsync();
                    Assert.Fail();
                }
                catch (RpcException e)
                {
                    Assert.AreEqual(StatusCode.Unknown, e.Status.StatusCode);
                    Assert.AreEqual(echoStatus.Message, e.Status.Detail);
                }
            }

            Console.WriteLine("Passed!");
        }

        private static async Task RunSpecialStatusMessageAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running special_status_message");

            var echoStatus = new EchoStatus
            {
                Code = 2,
                Message = "\t\ntest with whitespace\r\nand Unicode BMP ☺ and non-BMP 😈\t\n"
            };

            try
            {
                await client.UnaryCallAsync(new SimpleRequest
                {
                    ResponseStatus = echoStatus
                });
                Assert.Fail();
            }
            catch (RpcException e)
            {
                Assert.AreEqual(StatusCode.Unknown, e.Status.StatusCode);
                Assert.AreEqual(echoStatus.Message, e.Status.Detail);
            }

            Console.WriteLine("Passed!");
        }

        public static void RunUnimplementedService(UnimplementedService.UnimplementedServiceClient client)
        {
            Console.WriteLine("running unimplemented_service");
            var e = Assert.Throws<RpcException>(() => client.UnimplementedCall(new Empty()));

            Assert.AreEqual(StatusCode.Unimplemented, e.Status.StatusCode);
            Console.WriteLine("Passed!");
        }

        public static void RunUnimplementedMethod(TestService.TestServiceClient client)
        {
            Console.WriteLine("running unimplemented_method");
            var e = Assert.Throws<RpcException>(() => client.UnimplementedCall(new Empty()));

            Assert.AreEqual(StatusCode.Unimplemented, e.Status.StatusCode);
            Console.WriteLine("Passed!");
        }

        public static void RunClientCompressedUnary(TestService.TestServiceClient client)
        {
            Console.WriteLine("running client_compressed_unary");
            var probeRequest = new SimpleRequest
            {
                ExpectCompressed = new BoolValue
                {
                    Value = true  // lie about compression
                },
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828)
            };
            var e = Assert.Throws<RpcException>(() => client.UnaryCall(probeRequest, CreateClientCompressionMetadata(false)));
            Assert.AreEqual(StatusCode.InvalidArgument, e.Status.StatusCode);

            var compressedRequest = new SimpleRequest
            {
                ExpectCompressed = new BoolValue
                {
                    Value = true
                },
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828)
            };
            var response1 = client.UnaryCall(compressedRequest, CreateClientCompressionMetadata(true));
            Assert.AreEqual(314159, response1.Payload.Body.Length);

            var uncompressedRequest = new SimpleRequest
            {
                ExpectCompressed = new BoolValue
                {
                    Value = false
                },
                ResponseSize = 314159,
                Payload = CreateZerosPayload(271828)
            };
            var response2 = client.UnaryCall(uncompressedRequest, CreateClientCompressionMetadata(false));
            Assert.AreEqual(314159, response2.Payload.Body.Length);

            Console.WriteLine("Passed!");
        }

        public static async Task RunClientCompressedStreamingAsync(TestService.TestServiceClient client)
        {
            Console.WriteLine("running client_compressed_streaming");
            try
            {
                var probeCall = client.StreamingInputCall(CreateClientCompressionMetadata(false));
                await probeCall.RequestStream.WriteAsync(new StreamingInputCallRequest
                {
                    ExpectCompressed = new BoolValue
                    {
                        Value = true
                    },
                    Payload = CreateZerosPayload(27182)
                });

                // cannot use Assert.ThrowsAsync because it uses Task.Wait and would deadlock.
                await probeCall;
                Assert.Fail();
            }
            catch (RpcException e)
            {
                Assert.AreEqual(StatusCode.InvalidArgument, e.Status.StatusCode);
            }

            var call = client.StreamingInputCall(CreateClientCompressionMetadata(true));
            await call.RequestStream.WriteAsync(new StreamingInputCallRequest
            {
                ExpectCompressed = new BoolValue
                {
                    Value = true
                },
                Payload = CreateZerosPayload(27182)
            });

            call.RequestStream.WriteOptions = new WriteOptions(WriteFlags.NoCompress);
            await call.RequestStream.WriteAsync(new StreamingInputCallRequest
            {
                ExpectCompressed = new BoolValue
                {
                    Value = false
                },
                Payload = CreateZerosPayload(45904)
            });
            await call.RequestStream.CompleteAsync();

            var response = await call.ResponseAsync;
            Assert.AreEqual(73086, response.AggregatedPayloadSize);

            Console.WriteLine("Passed!");
        }

        private static Payload CreateZerosPayload(int size)
        {
            return new Payload { Body = ByteString.CopyFrom(new byte[size]) };
        }

        private static Metadata CreateClientCompressionMetadata(bool compressed)
        {
            var algorithmName = compressed ? "gzip" : "identity";
            return new Metadata
            {
                { new Metadata.Entry(Metadata.CompressionRequestAlgorithmMetadataKey, algorithmName) }
            };
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

        private static Metadata CreateTestMetadata()
        {
            return new Metadata
            {
                {"x-grpc-test-echo-initial", "test_initial_metadata_value"},
                {"x-grpc-test-echo-trailing-bin", new byte[] {0xab, 0xab, 0xab}}
            };
        }
    }
}
