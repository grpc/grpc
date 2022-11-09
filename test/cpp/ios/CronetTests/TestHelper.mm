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

#import "TestHelper.h"
#import <Cronet/Cronet.h>
#import <grpcpp/impl/codegen/string_ref.h>
#import <grpcpp/support/config.h>

using grpc::ServerContext;
using grpc::Status;
using grpc::testing::EchoRequest;
using grpc::testing::EchoResponse;
using grpc::testing::EchoTestService;
using std::chrono::system_clock;

std::atomic<int> PhonyInterceptor::num_times_run_;
std::atomic<int> PhonyInterceptor::num_times_run_reverse_;

std::string ToString(const grpc::string_ref& r) { return std::string(r.data(), r.size()); }

void configureCronet(void) {
  static dispatch_once_t configureCronet;
  dispatch_once(&configureCronet, ^{
    [Cronet setHttp2Enabled:YES];
    [Cronet setSslKeyLogFileName:@"Documents/key"];
    [Cronet enableTestCertVerifierForTesting];
    NSURL* url = [[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                         inDomains:NSUserDomainMask] lastObject];
    [Cronet start];
    [Cronet startNetLogToFile:@"cronet_netlog.json" logBytes:YES];
  });
}

bool CheckIsLocalhost(const std::string& addr) {
  const std::string kIpv6("[::1]:");
  const std::string kIpv4MappedIpv6("[::ffff:127.0.0.1]:");
  const std::string kIpv4("127.0.0.1:");
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
      grpc::Timepoint2Timespec(context->deadline(), &deadline);
    }
    response->mutable_param()->set_request_deadline(deadline.tv_sec);
  }
}

Status TestServiceImpl::Echo(ServerContext* context, const EchoRequest* request,
                             EchoResponse* response) {
  // A bit of sleep to make sure that short deadline tests fail
  if (request->has_param() && request->param().server_sleep_us() > 0) {
    gpr_sleep_until(
        gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                     gpr_time_from_micros(request->param().server_sleep_us(), GPR_TIMESPAN)));
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
      std::string serialized_debug_info = request->param().debug_info().SerializeAsString();
      context->AddTrailingMetadata(kDebugInfoTrailerKey, serialized_debug_info);
      return Status::CANCELLED;
    }
  }

  response->set_message(request->message());
  MaybeEchoDeadline(context, request, response);
  return Status::OK;
}

Status TestServiceImpl::RequestStream(ServerContext* context,
                                      grpc::ServerReader<EchoRequest>* reader,
                                      EchoResponse* response) {
  EchoRequest request;
  response->set_message("");
  int num_msgs_read = 0;
  while (reader->Read(&request)) {
    response->mutable_message()->append(request.message());
    ++num_msgs_read;
  }
  return Status::OK;
}

Status TestServiceImpl::ResponseStream(ServerContext* context, const EchoRequest* request,
                                       grpc::ServerWriter<EchoResponse>* writer) {
  EchoResponse response;
  int server_responses_to_send =
      GetIntValueFromMetadata(kServerResponseStreamsToSend, context->client_metadata(),
                              kServerDefaultResponseStreamsToSend);
  for (int i = 0; i < server_responses_to_send; i++) {
    response.set_message(request->message() + std::to_string(i));
    if (i == server_responses_to_send - 1) {
      writer->WriteLast(response, grpc::WriteOptions());
    } else {
      writer->Write(response);
    }
  }
  return Status::OK;
}

Status TestServiceImpl::BidiStream(ServerContext* context,
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
    response.set_message(request.message());
    if (read_counts == server_write_last) {
      stream->WriteLast(response, grpc::WriteOptions());
    } else {
      stream->Write(response);
    }
  }

  return Status::OK;
}

void PhonyInterceptor::Intercept(grpc::experimental::InterceptorBatchMethods* methods) {
  if (methods->QueryInterceptionHookPoint(
          grpc::experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
    num_times_run_++;
  } else if (methods->QueryInterceptionHookPoint(
                 grpc::experimental::InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
    num_times_run_reverse_++;
  }
  methods->Proceed();
}

void PhonyInterceptor::Reset() {
  num_times_run_.store(0);
  num_times_run_reverse_.store(0);
}

int PhonyInterceptor::GetNumTimesRun() {
  NSCAssert(num_times_run_.load() == num_times_run_reverse_.load(),
            @"Interceptor must run same number of times in both directions");
  return num_times_run_.load();
}
