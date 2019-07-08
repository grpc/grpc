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

#import <XCTest/XCTest.h>

#import <Cronet/Cronet.h>
#import <grpc/grpc_cronet.h>
#import <grpcpp/create_channel.h>
#import <grpcpp/impl/codegen/client_context.h>
#import <grpcpp/impl/codegen/config.h>
#import <grpcpp/resource_quota.h>
#import <grpcpp/security/auth_metadata_processor.h>
#import <grpcpp/security/credentials.h>
#import <grpcpp/server_builder.h>
#import <grpcpp/server_context.h>
#import <src/proto/grpc/testing/echo.grpc.pb.h>

#import "../ConfigureCronet.h"
#import "src/core/lib/security/credentials/credentials.h"
#import "test/core/end2end/data/ssl_test_data.h"

#import <map>
#import <sstream>
#import <thread>
#import <vector>

using namespace grpc::testing;
using std::chrono::system_clock;
using grpc::Status;
using grpc::ServerContext;
using grpc::ClientContext;

const char* const kServerFinishAfterNReads = "server_finish_after_n_reads";
const char* const kServerResponseStreamsToSend = "server_responses_to_send";
const int kServerDefaultResponseStreamsToSend = 3;

grpc::string ToString(const grpc::string_ref& r) { return grpc::string(r.data(), r.size()); }

bool MetadataContains(const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
                      const grpc::string& key, const grpc::string& value) {
  int count = 0;

  for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator iter = metadata.begin();
       iter != metadata.end(); ++iter) {
    if (ToString(iter->first) == key && ToString(iter->second) == value) {
      count++;
    }
  }
  return count == 1;
}

bool CheckIsLocalhost(const grpc::string& addr) {
  const grpc::string kIpv6("[::1]:");
  const grpc::string kIpv4MappedIpv6("[::ffff:127.0.0.1]:");
  const grpc::string kIpv4("127.0.0.1:");
  NSLog(@"Addr is: %s", addr.c_str());
  return addr.substr(0, kIpv4.size()) == kIpv4 ||
         addr.substr(0, kIpv4MappedIpv6.size()) == kIpv4MappedIpv6 ||
         addr.substr(0, kIpv6.size()) == kIpv6;
}

int GetIntValueFromMetadataHelper(const char* key,
                                  const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
                                  int default_value) {
  if (metadata.find(key) != metadata.end()) {
    std::istringstream iss(ToString(metadata.find(key)->second));
    iss >> default_value;
    NSLog(@"%s: %d", key, default_value);
  }

  return default_value;
}

int GetIntValueFromMetadata(const char* key,
                            const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
                            int default_value) {
  return GetIntValueFromMetadataHelper(key, metadata, default_value);
}

// When echo_deadline is requested, deadline seen in the ServerContext is set in
// the response in seconds.
void MaybeEchoDeadline(ServerContext* context, const EchoRequest* request, EchoResponse* response) {
  if (request->has_param() && request->param().echo_deadline()) {
    gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_REALTIME);
    if (context->deadline() != system_clock::time_point::max()) {
      NSLog(@"Calling Timepoint2Timespec");
      grpc::Timepoint2Timespec(context->deadline(), &deadline);
    }
    NSLog(@"Setting deadline = %d", deadline.tv_sec);
    response->mutable_param()->set_request_deadline(deadline.tv_sec);
  }
}

const char* const kDebugInfoTrailerKey = "debug-info-bin";
class TestServiceImpl : public EchoTestService::Service {
 public:
  Status Echo(ServerContext* context, const EchoRequest* request, EchoResponse* response) {
    // A bit of sleep to make sure that short deadline tests fail
    if (request->has_param() && request->param().server_sleep_us() > 0) {
      gpr_sleep_until(
          gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                       gpr_time_from_micros(request->param().server_sleep_us(), GPR_TIMESPAN)));
    }

    if (request->has_param() && request->param().server_die()) {
      gpr_log(GPR_ERROR, "The request should not reach application handler.");
      GPR_ASSERT(0);
    }
    if (request->has_param() && request->param().has_expected_error()) {
      const auto& error = request->param().expected_error();
      return Status(static_cast<grpc::StatusCode>(error.code()), error.error_message(),
                    error.binary_error_details());
    }

    if (request->has_param() && request->param().echo_metadata()) {
      const std::multimap<grpc::string_ref, grpc::string_ref>& client_metadata =
          context->client_metadata();
      for (std::multimap<grpc::string_ref, grpc::string_ref>::const_iterator iter =
               client_metadata.begin();
           iter != client_metadata.end(); ++iter) {
        context->AddTrailingMetadata(ToString(iter->first), ToString(iter->second));
      }
      // Terminate rpc with error and debug info in trailer.
      if (request->param().debug_info().stack_entries_size() ||
          !request->param().debug_info().detail().empty()) {
        grpc::string serialized_debug_info = request->param().debug_info().SerializeAsString();
        context->AddTrailingMetadata(kDebugInfoTrailerKey, serialized_debug_info);
        return Status::CANCELLED;
      }
    }

    response->set_message(request->message());
    MaybeEchoDeadline(context, request, response);
    return Status::OK;
  }

  Status RequestStream(ServerContext* context, grpc::ServerReader<EchoRequest>* reader,
                       EchoResponse* response) {
    EchoRequest request;
    response->set_message("");
    int num_msgs_read = 0;
    while (reader->Read(&request)) {
      response->mutable_message()->append(request.message());
      ++num_msgs_read;
    }
    NSLog(@"Read: %d messages", num_msgs_read);
    return Status::OK;
  }

  Status ResponseStream(ServerContext* context, const EchoRequest* request,
                        grpc::ServerWriter<EchoResponse>* writer) {
    EchoResponse response;
    int server_responses_to_send =
        GetIntValueFromMetadata(kServerResponseStreamsToSend, context->client_metadata(),
                                kServerDefaultResponseStreamsToSend);
    for (int i = 0; i < server_responses_to_send; i++) {
      response.set_message(request->message() + grpc::to_string(i));
      if (i == server_responses_to_send - 1) {
        writer->WriteLast(response, grpc::WriteOptions());
      } else {
        writer->Write(response);
      }
    }
    return Status::OK;
  }

  Status BidiStream(ServerContext* context,
                    grpc::ServerReaderWriter<EchoResponse, EchoRequest>* stream) {
    EchoRequest request;
    EchoResponse response;

    // kServerFinishAfterNReads suggests after how many reads, the server should
    // write the last message and send status (coalesced using WriteLast)
    int server_write_last =
        GetIntValueFromMetadata(kServerFinishAfterNReads, context->client_metadata(), 0);

    int read_counts = 0;
    while (stream->Read(&request)) {
      read_counts++;
      NSLog(@"recv msg %s", request.message().c_str());
      response.set_message(request.message());
      if (read_counts == server_write_last) {
        stream->WriteLast(response, grpc::WriteOptions());
      } else {
        stream->Write(response);
      }
    }

    return Status::OK;
  }
};
const char kTestCredsPluginErrorMsg[] = "Could not find plugin metadata.";
class TestMetadataCredentialsPlugin : public grpc::MetadataCredentialsPlugin {
 public:
  static const char kGoodMetadataKey[];
  static const char kBadMetadataKey[];

  TestMetadataCredentialsPlugin(const grpc::string_ref& metadata_key,
                                const grpc::string_ref& metadata_value, bool is_blocking,
                                bool is_successful, int delay_ms)
      : metadata_key_(metadata_key.data(), metadata_key.length()),
        metadata_value_(metadata_value.data(), metadata_value.length()),
        is_blocking_(is_blocking),
        is_successful_(is_successful),
        delay_ms_(delay_ms) {}

  bool IsBlocking() const override { return is_blocking_; }

  Status GetMetadata(grpc::string_ref service_url, grpc::string_ref method_name,
                     const grpc::AuthContext& channel_auth_context,
                     std::multimap<grpc::string, grpc::string>* metadata) override {
    if (delay_ms_ != 0) {
      gpr_sleep_until(
          gpr_time_add(gpr_now(GPR_CLOCK_REALTIME), gpr_time_from_millis(delay_ms_, GPR_TIMESPAN)));
    }
    NSCAssert(service_url.length() > 0UL, @"service_url is not empty");
    NSCAssert(method_name.length() > 0UL, @"method_name is not empty");
    NSCAssert(channel_auth_context.IsPeerAuthenticated(), @"peer is authenticated");
    NSCAssert(metadata != nullptr, @"metadata is not null");
    if (is_successful_) {
      metadata->insert(std::make_pair(metadata_key_, metadata_value_));
      return Status::OK;
    } else {
      return Status(grpc::StatusCode::NOT_FOUND, kTestCredsPluginErrorMsg);
    }
  }

 private:
  grpc::string metadata_key_;
  grpc::string metadata_value_;
  bool is_blocking_;
  bool is_successful_;
  int delay_ms_;
};
const char TestMetadataCredentialsPlugin::kBadMetadataKey[] = "TestPluginMetadata";
const char TestMetadataCredentialsPlugin::kGoodMetadataKey[] = "test-plugin-metadata";

class TestAuthMetadataProcessor : public grpc::AuthMetadataProcessor {
 public:
  static const char kGoodGuy[];

  TestAuthMetadataProcessor(bool is_blocking) : is_blocking_(is_blocking) {}

  std::shared_ptr<grpc::CallCredentials> GetCompatibleClientCreds() {
    return grpc::MetadataCredentialsFromPlugin(
        std::unique_ptr<grpc::MetadataCredentialsPlugin>(new TestMetadataCredentialsPlugin(
            TestMetadataCredentialsPlugin::kGoodMetadataKey, kGoodGuy, is_blocking_, true, 0)));
  }

  std::shared_ptr<grpc::CallCredentials> GetIncompatibleClientCreds() {
    return grpc::MetadataCredentialsFromPlugin(
        std::unique_ptr<grpc::MetadataCredentialsPlugin>(new TestMetadataCredentialsPlugin(
            TestMetadataCredentialsPlugin::kGoodMetadataKey, "Mr Hyde", is_blocking_, true, 0)));
  }

  // Interface implementation
  bool IsBlocking() const override { return is_blocking_; }

  Status Process(const InputMetadata& auth_metadata, grpc::AuthContext* context,
                 OutputMetadata* consumed_auth_metadata,
                 OutputMetadata* response_metadata) override {
    NSCAssert(consumed_auth_metadata != nullptr, @"consumed_auth_metadata cannot be null");
    NSCAssert(context != nullptr, @"context cannot be null");
    NSCAssert(response_metadata != nullptr, @"response_metadata cannot be null");
    auto auth_md = auth_metadata.find(TestMetadataCredentialsPlugin::kGoodMetadataKey);
    NSCAssert(auth_md != auth_metadata.end(), @"kGootMetadataKey must be present in auth_metadata");
    grpc::string_ref auth_md_value = auth_md->second;
    if (auth_md_value == kGoodGuy) {
      context->AddProperty(kIdentityPropName, kGoodGuy);
      context->SetPeerIdentityPropertyName(kIdentityPropName);
      consumed_auth_metadata->insert(
          std::make_pair(grpc::string(auth_md->first.data(), auth_md->first.length()),
                         grpc::string(auth_md->second.data(), auth_md->second.length())));
      return Status::OK;
    } else {
      return Status(grpc::StatusCode::UNAUTHENTICATED,
                    grpc::string("Invalid principal: ") +
                        grpc::string(auth_md_value.data(), auth_md_value.length()));
    }
  }

 private:
  static const char kIdentityPropName[];
  bool is_blocking_;
};

const char TestAuthMetadataProcessor::kGoodGuy[] = "Dr Jekyll";
const char TestAuthMetadataProcessor::kIdentityPropName[] = "novel identity";

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

- (void)startServerWithAuthMetadataProcessor:
    (const std::shared_ptr<grpc::AuthMetadataProcessor>&)processor {
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
  server_creds->SetAuthMetadataProcessor(processor);
  builder.AddListeningPort("localhost:5000", server_creds);
  builder.RegisterService(&_service);
  builder.RegisterService("foo.test.youtube.com", &_foo_service);
  _server = builder.BuildAndStart();
}

- (void)startServer {
  [self startServerWithAuthMetadataProcessor:std::shared_ptr<grpc::AuthMetadataProcessor>()];
}

- (void)stopServer {
  _server.reset();
}

- (void)restartServer {
  [self restartServerWithAuthMetadataProcessor:std::shared_ptr<grpc::AuthMetadataProcessor>()];
}

- (void)restartServerWithAuthMetadataProcessor:
    (const std::shared_ptr<grpc::AuthMetadataProcessor>&)processor {
  [self stopServer];
  [self startServerWithAuthMetadataProcessor:processor];
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
      context.AddMetadata("custom-bin", grpc::string(bytes, 8));
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
  NSLog(@"Response: %s ok: %d", response.message().c_str(), s.ok());

  const auto& trailing_metadata = context.GetServerTrailingMetadata();
  auto iter = trailing_metadata.find("user-agent");
  NSLog(@"User-agent: %s", iter->second.data());
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
    XCTAssertEqual(response.message(), request.message() + grpc::to_string(i));
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
  grpc::string msg("hello");

  auto stream = stub->BidiStream(&context);

  for (int i = 0; i < kServerDefaultResponseStreamsToSend; ++i) {
    request.set_message(msg + grpc::to_string(i));
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
  grpc::string msg("hello");

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
  grpc::string msg("hello");

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
  grpc::string expected_string = info->SerializeAsString();
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

- (void)testNonBlockingAuthMetadataPluginAndProcessorSuccess {
  auto stub = [self getStub];
  EchoRequest request;
  EchoResponse response;
  ClientContext context;
  auto* processor = new TestAuthMetadataProcessor(false);
  [self restartServerWithAuthMetadataProcessor:std::shared_ptr<grpc::AuthMetadataProcessor>(
                                                   processor)];
  context.set_credentials(processor->GetCompatibleClientCreds());
  request.set_message("Hello");
  request.mutable_param()->set_echo_metadata(true);
  request.mutable_param()->set_expected_client_identity(TestAuthMetadataProcessor::kGoodGuy);
  request.mutable_param()->set_expected_transport_security_type("ssl");

  Status s = stub->Echo(&context, request, &response);
  XCTAssertEqual(request.message(), response.message());
  XCTAssertTrue(s.ok());

  // Metadata should have been consumed by the processor.
  XCTAssertFalse(MetadataContains(context.GetServerTrailingMetadata(),
                                  GRPC_AUTHORIZATION_METADATA_KEY,
                                  grpc::string("Bearer ") + TestAuthMetadataProcessor::kGoodGuy));
}

@end
