//
//
// Copyright 2026 gRPC authors.
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
//
//

#ifndef GRPC_TEST_CPP_END2END_END2END_TEST_UTILS_H
#define GRPC_TEST_CPP_END2END_END2END_TEST_UTILS_H

#include <grpc/grpc.h>
#include <grpcpp/impl/generic_stub_session.h>
#include <grpcpp/support/channel_arguments.h>

#include <memory>
#include <string>
#include <utility>

#include "src/core/lib/experiments/experiments.h"
#include "src/core/util/grpc_check.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/synchronization/notification.h"

namespace grpc {
namespace testing {

inline bool IsPh2Test() {
  return grpc_core::IsPh2ClientEnabled() || grpc_core::IsPh2ServerEnabled() ||
         grpc_core::IsPh2ClientServerEnabled();
}

// TODO(tjagtap) : [PH2][P3] : Remove once all the PH2 E2E tests are fixed.
inline void DisableLoggingForPH2Tests() {
  if (IsPh2Test()) {
    grpc_tracer_set_enabled("http", false);
    grpc_tracer_set_enabled("channel", false);
    grpc_tracer_set_enabled("subchannel", false);
    grpc_tracer_set_enabled("client_channel", false);
    grpc_tracer_set_enabled("http2_ph2_transport", false);
    grpc_tracer_set_enabled("call", false);
    grpc_tracer_set_enabled("call_state", false);
    grpc_tracer_set_enabled("promise_primitives", false);
    absl::SetGlobalVLogLevel(-1);
  }
}

// TODO(tjagtap) : [PH2][P3] : Remove once all the PH2 E2E tests are fixed.
inline void EnableLoggingForPH2Tests() {
  if (IsPh2Test()) {
    grpc_tracer_set_enabled("http", 1);
    grpc_tracer_set_enabled("channel", 1);
    grpc_tracer_set_enabled("subchannel", 1);
    grpc_tracer_set_enabled("client_channel", 1);
    grpc_tracer_set_enabled("http2_ph2_transport", 1);
    grpc_tracer_set_enabled("call", 1);
    grpc_tracer_set_enabled("call_state", 1);
    grpc_tracer_set_enabled("promise_primitives", 1);
    absl::SetGlobalVLogLevel(2);
  }
}

inline void ApplyCommonChannelArguments(ChannelArguments& args) {
  if (grpc_core::IsPh2ClientEnabled() ||
      grpc_core::IsPh2ClientServerEnabled()) {
    // TODO(tjagtap) [PH2][P5][Retry] Consider removing when bug in
    // retry_interceptor.cc is fixed.
    args.SetInt(GRPC_ARG_ENABLE_RETRIES, 0);
  }
}

class TestClientSessionReactor
    : public grpc::experimental::ClientSessionReactor {
 public:
  explicit TestClientSessionReactor(absl::Notification* session_done = nullptr)
      : session_done_(session_done) {}

  void OnSessionReady(std::shared_ptr<grpc::Channel> virtual_channel) override {
    virtual_channel_ = std::move(virtual_channel);
    ready_.Notify();
  }
  void OnSessionAcknowledged(bool ok) override { acked_.Notify(); }
  void OnDone(const grpc::Status& s) override {
    status_ = s;
    if (!ready_.HasBeenNotified()) {
      ready_.Notify();
    }
    caller_done_.WaitForNotification();
    if (session_done_ != nullptr) {
      session_done_->Notify();
    }
    delete this;
  }

  std::shared_ptr<grpc::Channel> virtual_channel() { return virtual_channel_; }
  void WaitForReady() {
    ready_.WaitForNotification();
    GRPC_CHECK(virtual_channel_ != nullptr)
        << "Session failed with status: " << status_.error_message() << " ("
        << status_.error_code() << ")";
  }
  void SignalCallerDone() { caller_done_.Notify(); }

 private:
  std::shared_ptr<grpc::Channel> virtual_channel_;
  absl::Notification ready_;
  absl::Notification acked_;
  absl::Notification caller_done_;
  grpc::Status status_;
  absl::Notification* session_done_;
};

template <typename RequestType, typename ResponseType>
std::shared_ptr<grpc::Channel> MaybeWrapVirtualChannel(
    std::shared_ptr<grpc::Channel> channel, const grpc::ChannelArguments& args,
    bool use_virtual_rpcs, grpc::ClientContext* context, RequestType* request,
    absl::Notification* session_done = nullptr,
    const std::string& method_name =
        "/grpc.testing.EchoTestService/SessionRequest") {
  if (!use_virtual_rpcs) return channel;

  auto session_stub = std::make_unique<
      grpc::experimental::GenericStubSession<RequestType, ResponseType>>(
      channel);
  auto* session_reactor = new TestClientSessionReactor(session_done);
  session_stub->PrepareSessionCall(context, method_name, {}, request,
                                   session_reactor, args);
  session_reactor->StartCall();
  session_reactor->WaitForReady();
  auto vchannel = session_reactor->virtual_channel();
  session_reactor->SignalCallerDone();
  return vchannel;
}

#define SKIP_IF_VIRTUAL()                  \
  if (GetParam().use_virtual_rpcs())       \
    GTEST_SKIP() << "Skipped for Virtual " \
                    "RPCs";

#define SKIP_TEST_FOR_PH2_CLIENT(message)    \
  if (grpc_core::IsPh2ClientEnabled() ||     \
      grpc_core::IsPh2ClientServerEnabled()) \
    GTEST_SKIP() << (message);

#define SKIP_TEST_FOR_PH2_SERVER(message)    \
  if (grpc_core::IsPh2ServerEnabled() ||     \
      grpc_core::IsPh2ClientServerEnabled()) \
    GTEST_SKIP() << (message);

#define SKIP_TEST_FOR_PH2(message) \
  if (IsPh2Test()) GTEST_SKIP() << (message);

// Retry for PH2 will be implemented separately, after the PH2 Client and
// Server rollout starts.
#define SKIP_RETRY_TEST_FOR_PH2_CLIENT(message) \
  if (grpc_core::IsPh2ClientEnabled() ||        \
      grpc_core::IsPh2ClientServerEnabled())    \
    GTEST_SKIP() << (message);

}  // namespace testing
}  // namespace grpc

#endif  // GRPC_TEST_CPP_END2END_END2END_TEST_UTILS_H
