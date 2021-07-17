/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_TEST_CPP_INTEROP_INTEROP_CLIENT_H
#define GRPC_TEST_CPP_INTEROP_INTEROP_CLIENT_H

#include <memory>

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"

namespace grpc {
namespace testing {

// Function pointer for custom checks.
typedef std::function<void(const InteropClientContextInspector&,
                           const SimpleRequest*, const SimpleResponse*)>
    CheckerFn;

typedef std::function<std::shared_ptr<Channel>(void)> ChannelCreationFunc;

class InteropClient {
 public:
  /// If new_stub_every_test_case is true, a new TestService::Stub object is
  /// created for every test case
  /// If do_not_abort_on_transient_failures is true, abort() is not called in
  /// case of transient failures (like connection failures)
  explicit InteropClient(ChannelCreationFunc channel_creation_func,
                         bool new_stub_every_test_case,
                         bool do_not_abort_on_transient_failures);
  ~InteropClient() {}

  void Reset(const std::shared_ptr<Channel>& channel);

  bool DoEmpty();
  bool DoLargeUnary();
  bool DoServerCompressedUnary();
  bool DoClientCompressedUnary();
  bool DoPingPong();
  bool DoHalfDuplex();
  bool DoRequestStreaming();
  bool DoResponseStreaming();
  bool DoServerCompressedStreaming();
  bool DoClientCompressedStreaming();
  bool DoResponseStreamingWithSlowConsumer();
  bool DoCancelAfterBegin();
  bool DoCancelAfterFirstResponse();
  bool DoTimeoutOnSleepingServer();
  bool DoEmptyStream();
  bool DoStatusWithMessage();
  // Verifies Unicode and Whitespace is correctly processed in status message.
  bool DoSpecialStatusMessage();
  bool DoCustomMetadata();
  bool DoUnimplementedMethod();
  bool DoUnimplementedService();
  bool DoCacheableUnary();
  // all requests are sent to one server despite multiple servers are resolved
  bool DoPickFirstUnary();

  // The following interop test are not yet part of the interop spec, and are
  // not implemented cross-language. They are considered experimental for now,
  // but at some point in the future, might be codified and implemented in all
  // languages
  bool DoChannelSoakTest(int32_t soak_iterations, int32_t max_failures,
                         int64_t max_acceptable_per_iteration_latency_ms,
                         int32_t overall_timeout_seconds);
  bool DoRpcSoakTest(int32_t soak_iterations, int32_t max_failures,
                     int64_t max_acceptable_per_iteration_latency_ms,
                     int32_t overall_timeout_seconds);
  bool DoLongLivedChannelTest(int32_t soak_iterations,
                              int32_t iteration_interval);

  // Auth tests.
  // username is a string containing the user email
  bool DoJwtTokenCreds(const std::string& username);
  bool DoComputeEngineCreds(const std::string& default_service_account,
                            const std::string& oauth_scope);
  // username the GCE default service account email
  bool DoOauth2AuthToken(const std::string& username,
                         const std::string& oauth_scope);
  // username is a string containing the user email
  bool DoPerRpcCreds(const std::string& json_key);
  // default_service_account is the GCE default service account email
  bool DoGoogleDefaultCredentials(const std::string& default_service_account);

 private:
  class ServiceStub {
   public:
    // If new_stub_every_call = true, pointer to a new instance of
    // TestServce::Stub is returned by Get() everytime it is called
    ServiceStub(ChannelCreationFunc channel_creation_func,
                bool new_stub_every_call);

    TestService::Stub* Get();
    UnimplementedService::Stub* GetUnimplementedServiceStub();

    // forces channel to be recreated.
    void ResetChannel();

   private:
    ChannelCreationFunc channel_creation_func_;
    std::unique_ptr<TestService::Stub> stub_;
    std::unique_ptr<UnimplementedService::Stub> unimplemented_service_stub_;
    std::shared_ptr<Channel> channel_;
    bool new_stub_every_call_;  // If true, a new stub is returned by every
                                // Get() call
  };

  bool PerformLargeUnary(SimpleRequest* request, SimpleResponse* response);

  /// Run \a custom_check_fn as an additional check.
  bool PerformLargeUnary(SimpleRequest* request, SimpleResponse* response,
                         const CheckerFn& custom_checks_fn);
  bool AssertStatusOk(const Status& s,
                      const std::string& optional_debug_string);
  bool AssertStatusCode(const Status& s, StatusCode expected_code,
                        const std::string& optional_debug_string);
  bool TransientFailureOrAbort();

  std::tuple<bool, int32_t, std::string> PerformOneSoakTestIteration(
      const bool reset_channel,
      const int32_t max_acceptable_per_iteration_latency_ms);

  void PerformSoakTest(const bool reset_channel_per_iteration,
                       const int32_t soak_iterations,
                       const int32_t max_failures,
                       const int32_t max_acceptable_per_iteration_latency_ms,
                       const int32_t overall_timeout_seconds);

  ServiceStub serviceStub_;
  /// If true, abort() is not called for transient failures
  bool do_not_abort_on_transient_failures_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_INTEROP_INTEROP_CLIENT_H
