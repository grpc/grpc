/*
 *
 * Copyright 2019 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#import <Cronet/Cronet.h>
#import <XCTest/XCTest.h>

#import <grpc/grpc_cronet.h>
#import <grpcpp/client_context.h>
#import <grpcpp/create_channel.h>
#import <grpcpp/resource_quota.h>
#import <grpcpp/security/cronet_credentials.h>
#import <grpcpp/server_builder.h>
#import <grpcpp/server_context.h>
#import <grpcpp/support/client_interceptor.h>
#import <grpcpp/support/config.h>
#import "src/proto/grpc/testing/echo.grpc.pb.h"

#import "TestHelper.h"
#import "test/core/end2end/data/ssl_test_data.h"

#import <map>
#import <sstream>
#import <thread>
#import <vector>

using namespace grpc::testing;
using grpc::ClientContext;
using grpc::ServerContext;
using grpc::Status;
using std::chrono::system_clock;

@interface CppCronetEnd2EndTests : XCTestCase

@end

@implementation CppCronetEnd2EndTests {
  std::unique_ptr<grpc::Server> _server;
  TestServiceImpl _service;
  TestServiceImpl _foo_service;
}

// The setUp() function is run before the test cases run and only run once
+ (void)setUp {
  [super setUp];
  configureCronet();
}

- (void)startServer {
  if (_server) {
    // server is already running
    return;
  }

  grpc::ServerBuilder builder;
  grpc::SslServerCredentialsOptions ssl_opts;

  ssl_opts.pem_root_certs = "";
  grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp = {test_server1_key, test_server1_cert};
  ssl_opts.pem_key_cert_pairs.push_back(pkcp);
  auto server_creds = SslServerCredentials(ssl_opts);
  builder.AddListeningPort("localhost:5000", server_creds);
  builder.RegisterService(&_service);
  builder.RegisterService("foo.test.youtube.com", &_foo_service);
  _server = builder.BuildAndStart();
}

- (void)stopServer {
  _server.reset();
}

- (void)restartServer {
  [self stopServer];
  [self startServer];
}

- (void)setUp {
  [self startServer];
}

- (void)sendRPCWithStub:(EchoTestService::Stub*)stub
                numRPCs:(int)num_rpcs
     withBinaryMetadata:(BOOL)with_binary_metadata {
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello hello hello hello");

  for (int i = 0; i < num_rpcs; ++i) {
    ClientContext context;
    if (with_binary_metadata) {
      char bytes[8] = {'\0', '\1', '\2', '\3', '\4', '\5', '\6', static_cast<char>(i)};
      context.AddMetadata("custom-bin", std::string(bytes, 8));
    }
    context.set_compression_algorithm(GRPC_COMPRESS_GZIP);
    Status s = stub->Echo(&context, request, &response);
    XCTAssertEqual(response.message(), request.message());
    XCTAssertTrue(s.ok());
  }
}

- (std::shared_ptr<::grpc::Channel>)getChannel {
  stream_engine* cronetEngine = [Cronet getGlobalEngine];
  auto cronetChannelCredentials = grpc::CronetChannelCredentials(cronetEngine);
  grpc::ChannelArguments args;
  args.SetSslTargetNameOverride("foo.test.google.fr");
  args.SetUserAgentPrefix("custom_prefix");
  args.SetString(GRPC_ARG_SECONDARY_USER_AGENT_STRING, "end2end_test");
  auto channel = grpc::CreateCustomChannel("127.0.0.1:5000", cronetChannelCredentials, args);
  return channel;
}

- (std::shared_ptr<::grpc::Channel>)getChannelWithInterceptors:
    (std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>)creators {
  stream_engine* cronetEngine = [Cronet getGlobalEngine];
  auto cronetChannelCredentials = grpc::CronetChannelCredentials(cronetEngine);
  grpc::ChannelArguments args;
  args.SetSslTargetNameOverride("foo.test.google.fr");
  args.SetUserAgentPrefix("custom_prefix");
  args.SetString(GRPC_ARG_SECONDARY_USER_AGENT_STRING, "end2end_test");
  auto channel = grpc::experimental::CreateCustomChannelWithInterceptors(
      "127.0.01:5000", cronetChannelCredentials, args, std::move(creators));
  return channel;
}

- (std::unique_ptr<EchoTestService::Stub>)getStub {
  auto channel = [self getChannel];
  auto stub = EchoTestService::NewStub(channel);
  return stub;
}

- (void)testUserAgent {
  ClientContext context;
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_echo_metadata(true);
  auto stub = [self getStub];
  Status s = stub->Echo(&context, request, &response);
  XCTAssertTrue(s.ok());
  const auto& trailing_metadata = context.GetServerTrailingMetadata();
  auto iter = trailing_metadata.find("user-agent");
  XCTAssert(iter->second.starts_with("custom_prefix grpc-c++"));
}

- (void)testMultipleRPCs {
  auto stub = [self getStub];
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(
        [self, &stub]() { [self sendRPCWithStub:stub.get() numRPCs:10 withBinaryMetadata:NO]; });
  }
  for (int i = 0; i < 10; ++i) {
    threads[i].join();
  }
}

- (void)testMultipleRPCsWithBinaryMetadata {
  auto stub = [self getStub];
  std::vector<std::thread> threads;
  threads.reserve(10);
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back(
        [self, &stub]() { [self sendRPCWithStub:stub.get() numRPCs:10 withBinaryMetadata:YES]; });
  }
  for (int i = 0; i < 10; ++i) {
    threads[i].join();
  }
}

- (void)testEmptyBinaryMetadata {
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello hello hello hello");
  ClientContext context;
  context.AddMetadata("custom-bin", "");
  auto stub = [self getStub];
  Status s = stub->Echo(&context, request, &response);
  XCTAssertEqual(response.message(), request.message());
  XCTAssertTrue(s.ok());
}

- (void)testReconnectChannel {
  auto stub = [self getStub];
  [self sendRPCWithStub:stub.get() numRPCs:1 withBinaryMetadata:NO];

  [self restartServer];
  [self sendRPCWithStub:stub.get() numRPCs:1 withBinaryMetadata:NO];
}

- (void)testRequestStreamOneRequest {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  auto stream = stub->RequestStream(&context, &response);
  request.set_message("hello");
  XCTAssertTrue(stream->Write(request));
  stream->WritesDone();
  Status s = stream->Finish();
  XCTAssertEqual(response.message(), request.message());
  XCTAssertTrue(s.ok());
  XCTAssertTrue(context.debug_error_string().empty());
}

- (void)testRequestStreamOneRequestWithCoalescingApi {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.set_initial_metadata_corked(true);
  auto stream = stub->RequestStream(&context, &response);
  request.set_message("hello");
  XCTAssertTrue(stream->Write(request));
  stream->WritesDone();
  Status s = stream->Finish();
  XCTAssertEqual(response.message(), request.message());
  XCTAssertTrue(s.ok());
}

- (void)testRequestStreamTwoRequests {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  auto stream = stub->RequestStream(&context, &response);
  request.set_message("hello");
  XCTAssertTrue(stream->Write(request));
  XCTAssertTrue(stream->Write(request));
  stream->WritesDone();
  Status s = stream->Finish();
  XCTAssertEqual(response.message(), "hellohello");
  XCTAssertTrue(s.ok());
}

- (void)testResponseStream {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");

  auto stream = stub->ResponseStream(&context, request);
  for (int i = 0; i < kServerDefaultResponseStreamsToSend; ++i) {
    XCTAssertTrue(stream->Read(&response));
    XCTAssertEqual(response.message(), request.message() + std::to_string(i));
  }
  XCTAssertFalse(stream->Read(&response));

  Status s = stream->Finish();
  XCTAssertTrue(s.ok());
}

- (void)testBidiStream {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  std::string msg("hello");

  auto stream = stub->BidiStream(&context);

  for (int i = 0; i < kServerDefaultResponseStreamsToSend; ++i) {
    request.set_message(msg + std::to_string(i));
    XCTAssertTrue(stream->Write(request));
    XCTAssertTrue(stream->Read(&response));
    XCTAssertEqual(response.message(), request.message());
  }

  stream->WritesDone();
  XCTAssertFalse(stream->Read(&response));
  XCTAssertFalse(stream->Read(&response));

  Status s = stream->Finish();
  XCTAssertTrue(s.ok());
}

- (void)testBidiStreamWithCoalescingApi {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  context.AddMetadata(kServerFinishAfterNReads, "3");
  context.set_initial_metadata_corked(true);
  std::string msg("hello");

  auto stream = stub->BidiStream(&context);

  request.set_message(msg + "0");
  XCTAssertTrue(stream->Write(request));
  XCTAssertTrue(stream->Read(&response));
  XCTAssertEqual(response.message(), request.message());

  request.set_message(msg + "1");
  XCTAssertTrue(stream->Write(request));
  XCTAssertTrue(stream->Read(&response));
  XCTAssertEqual(response.message(), request.message());

  request.set_message(msg + "2");
  stream->WriteLast(request, grpc::WriteOptions());
  XCTAssertTrue(stream->Read(&response));
  XCTAssertEqual(response.message(), request.message());

  XCTAssertFalse(stream->Read(&response));
  XCTAssertFalse(stream->Read(&response));

  Status s = stream->Finish();
  XCTAssertTrue(s.ok());
}

- (void)testCancelBeforeStart {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");
  context.TryCancel();
  Status s = stub->Echo(&context, request, &response);
  XCTAssertEqual("", response.message());
  XCTAssertEqual(grpc::StatusCode::CANCELLED, s.error_code());
}

- (void)testClientCancelsRequestStream {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");

  auto stream = stub->RequestStream(&context, &response);
  XCTAssertTrue(stream->Write(request));
  XCTAssertTrue(stream->Write(request));

  context.TryCancel();

  Status s = stream->Finish();
  XCTAssertEqual(grpc::StatusCode::CANCELLED, s.error_code());
  XCTAssertEqual(response.message(), "");
}

- (void)testClientCancelsResponseStream {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("hello");

  auto stream = stub->ResponseStream(&context, request);

  XCTAssertTrue(stream->Read(&response));
  XCTAssertEqual(response.message(), request.message() + "0");
  XCTAssertTrue(stream->Read(&response));
  XCTAssertEqual(response.message(), request.message() + "1");

  context.TryCancel();

  // The cancellation races with responses, so there might be zero or
  // one responses pending, read till failure

  if (stream->Read(&response)) {
    XCTAssertEqual(response.message(), request.message() + "2");
    // Since we have cancelled, we expect the next attempt to read to fail
    XCTAssertFalse(stream->Read(&response));
  }
}

- (void)testlClientCancelsBidiStream {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  std::string msg("hello");

  auto stream = stub->BidiStream(&context);

  request.set_message(msg + "0");
  XCTAssertTrue(stream->Write(request));
  XCTAssertTrue(stream->Read(&response));
  XCTAssertEqual(response.message(), request.message());

  request.set_message(msg + "1");
  XCTAssertTrue(stream->Write(request));

  context.TryCancel();

  // The cancellation races with responses, so there might be zero or
  // one responses pending, read till failure

  if (stream->Read(&response)) {
    XCTAssertEqual(response.message(), request.message());
    // Since we have cancelled, we expect the next attempt to read to fail
    XCTAssertFalse(stream->Read(&response));
  }

  Status s = stream->Finish();
  XCTAssertEqual(grpc::StatusCode::CANCELLED, s.error_code());
}

- (void)testNonExistingService {
  auto channel = [self getChannel];
  auto stub = grpc::testing::UnimplementedEchoService::NewStub(channel);

  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  Status s = stub->Unimplemented(&context, request, &response);
  XCTAssertEqual(grpc::StatusCode::UNIMPLEMENTED, s.error_code());
  XCTAssertEqual("", s.error_message());
}

- (void)testBinaryTrailer {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;

  request.mutable_param()->set_echo_metadata(true);
  DebugInfo* info = request.mutable_param()->mutable_debug_info();
  info->add_stack_entries("stack_entry_1");
  info->add_stack_entries("stack_entry_2");
  info->add_stack_entries("stack_entry_3");
  info->set_detail("detailed debug info");
  std::string expected_string = info->SerializeAsString();
  request.set_message("Hello");

  Status s = stub->Echo(&context, request, &response);
  XCTAssertFalse(s.ok());
  auto trailers = context.GetServerTrailingMetadata();
  XCTAssertEqual(1u, trailers.count(kDebugInfoTrailerKey));
  auto iter = trailers.find(kDebugInfoTrailerKey);
  XCTAssertEqual(expected_string, iter->second);
  // Parse the returned trailer into a DebugInfo proto.
  DebugInfo returned_info;
  XCTAssertTrue(returned_info.ParseFromString(ToString(iter->second)));
}

- (void)testExpectError {
  auto stub = [self getStub];
  std::vector<ErrorStatus> expected_status;
  expected_status.emplace_back();
  expected_status.back().set_code(13);  // INTERNAL
  // No Error message or details

  expected_status.emplace_back();
  expected_status.back().set_code(13);  // INTERNAL
  expected_status.back().set_error_message("text error message");
  expected_status.back().set_binary_error_details("text error details");

  expected_status.emplace_back();
  expected_status.back().set_code(13);  // INTERNAL
  expected_status.back().set_error_message("text error message");
  expected_status.back().set_binary_error_details("\x0\x1\x2\x3\x4\x5\x6\x8\x9\xA\xB");

  for (auto iter = expected_status.begin(); iter != expected_status.end(); ++iter) {
    EchoRequest request;
    EchoResponse response;
    ClientContext context;
    request.set_message("Hello");
    auto* error = request.mutable_param()->mutable_expected_error();
    error->set_code(iter->code());
    error->set_error_message(iter->error_message());
    error->set_binary_error_details(iter->binary_error_details());

    Status s = stub->Echo(&context, request, &response);
    XCTAssertFalse(s.ok());
    XCTAssertEqual(iter->code(), s.error_code());
    XCTAssertEqual(iter->error_message(), s.error_message());
    XCTAssertEqual(iter->binary_error_details(), s.error_details());
    XCTAssertTrue(context.debug_error_string().find("created") != std::string::npos);
    XCTAssertTrue(context.debug_error_string().find("file") != std::string::npos);
    XCTAssertTrue(context.debug_error_string().find("line") != std::string::npos);
    XCTAssertTrue(context.debug_error_string().find("status") != std::string::npos);
    XCTAssertTrue(context.debug_error_string().find("13") != std::string::npos);
  }
}

- (void)testRpcDeadlineExpires {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_skip_cancelled_check(true);
  // Let server sleep for 40 ms first to guarantee expiry.
  request.mutable_param()->set_server_sleep_us(40 * 1000);

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::milliseconds(1);
  context.set_deadline(deadline);
  Status s = stub->Echo(&context, request, &response);
  XCTAssertEqual(grpc::StatusCode::DEADLINE_EXCEEDED, s.error_code());
}

- (void)testRpcLongDeadline {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::hours(1);
  context.set_deadline(deadline);
  Status s = stub->Echo(&context, request, &response);
  XCTAssertEqual(response.message(), request.message());
  XCTAssertTrue(s.ok());
}

- (void)testEchoDeadlineForNoDeadlineRpc {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_echo_deadline(true);

  ClientContext context;
  Status s = stub->Echo(&context, request, &response);
  XCTAssertEqual(response.message(), request.message());
  XCTAssertTrue(s.ok());
  XCTAssertEqual(response.param().request_deadline(), gpr_inf_future(GPR_CLOCK_REALTIME).tv_sec);
}

- (void)testEchoDeadline {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  request.mutable_param()->set_echo_deadline(true);

  ClientContext context;
  std::chrono::system_clock::time_point deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(100);
  context.set_deadline(deadline);
  Status s = stub->Echo(&context, request, &response);
  XCTAssertEqual(response.message(), request.message());
  XCTAssertTrue(s.ok());
  gpr_timespec sent_deadline;
  grpc::Timepoint2Timespec(deadline, &sent_deadline);
  // We want to allow some reasonable error given:
  // - request_deadline() only has 1sec resolution so the best we can do is +-1
  // - if sent_deadline.tv_nsec is very close to the next second's boundary we
  // can end up being off by 2 in one direction.
  XCTAssertLessThanOrEqual(response.param().request_deadline() - sent_deadline.tv_sec, 2);
  XCTAssertGreaterThanOrEqual(response.param().request_deadline() - sent_deadline.tv_sec, -1);
  NSLog(@"request deadline: %d sent_deadline: %d", response.param().request_deadline(),
        sent_deadline.tv_sec);
}

- (void)testPeer {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  ClientContext context;
  Status s = stub->Echo(&context, request, &response);
  XCTAssertTrue(s.ok());
  XCTAssertTrue(CheckIsLocalhost(context.peer()));
}

- (void)testClientInterceptor {
  PhonyInterceptor::Reset();
  std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>> creators;
  // Add 20 phony interceptors
  for (auto i = 0; i < 20; i++) {
    creators.push_back(std::unique_ptr<PhonyInterceptorFactory>(new PhonyInterceptorFactory()));
  }
  auto channel = [self getChannelWithInterceptors:std::move(creators)];
  auto stub = EchoTestService::NewStub(channel);

  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  request.set_message("Hello");
  Status s = stub->Echo(&context, request, &response);
  XCTAssertTrue(s.ok());
  XCTAssertEqual(PhonyInterceptor::GetNumTimesRun(), 20);
}

@end
