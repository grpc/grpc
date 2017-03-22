/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_TEST_CPP_INTEROP_INTEROP_CLIENT_H
#define GRPC_TEST_CPP_INTEROP_INTEROP_CLIENT_H

#include <memory>

#include <grpc++/channel.h>
#include <grpc/grpc.h>
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"

namespace grpc {
namespace testing {

// Function pointer for custom checks.
typedef std::function<void(const InteropClientContextInspector&,
                           const SimpleRequest*, const SimpleResponse*)>
    CheckerFn;

class InteropClient {
 public:
  /// If new_stub_every_test_case is true, a new TestService::Stub object is
  /// created for every test case
  /// If do_not_abort_on_transient_failures is true, abort() is not called in
  /// case of transient failures (like connection failures)
  explicit InteropClient(std::shared_ptr<Channel> channel,
                         bool new_stub_every_test_case,
                         bool do_not_abort_on_transient_failures);
  ~InteropClient() {}

  void Reset(std::shared_ptr<Channel> channel);

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
  bool DoCustomMetadata();
  bool DoUnimplementedMethod();
  bool DoUnimplementedService();
  bool DoCacheableUnary();
  // Auth tests.
  // username is a string containing the user email
  bool DoJwtTokenCreds(const grpc::string& username);
  bool DoComputeEngineCreds(const grpc::string& default_service_account,
                            const grpc::string& oauth_scope);
  // username the GCE default service account email
  bool DoOauth2AuthToken(const grpc::string& username,
                         const grpc::string& oauth_scope);
  // username is a string containing the user email
  bool DoPerRpcCreds(const grpc::string& json_key);

 private:
  class ServiceStub {
   public:
    // If new_stub_every_call = true, pointer to a new instance of
    // TestServce::Stub is returned by Get() everytime it is called
    ServiceStub(std::shared_ptr<Channel> channel, bool new_stub_every_call);

    TestService::Stub* Get();
    UnimplementedService::Stub* GetUnimplementedServiceStub();

    void Reset(std::shared_ptr<Channel> channel);

   private:
    std::unique_ptr<TestService::Stub> stub_;
    std::unique_ptr<UnimplementedService::Stub> unimplemented_service_stub_;
    std::shared_ptr<Channel> channel_;
    bool new_stub_every_call_;  // If true, a new stub is returned by every
                                // Get() call
  };

  bool PerformLargeUnary(SimpleRequest* request, SimpleResponse* response);

  /// Run \a custom_check_fn as an additional check.
  bool PerformLargeUnary(SimpleRequest* request, SimpleResponse* response,
                         CheckerFn custom_checks_fn);
  bool AssertStatusOk(const Status& s);
  bool AssertStatusCode(const Status& s, StatusCode expected_code);
  bool TransientFailureOrAbort();
  ServiceStub serviceStub_;

  /// If true, abort() is not called for transient failures
  bool do_not_abort_on_transient_failures_;
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_INTEROP_INTEROP_CLIENT_H
