//
//
// Copyright 2016 gRPC authors.
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

#include <stddef.h>

#include <algorithm>
#include <initializer_list>
#include <vector>

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/status.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/end2end/end2end_tests.h"

namespace grpc_core {
namespace {

const int kNumCalls = 8;
const int kClientBaseTag = 1000;
const int kServerStartBaseTag = 2000;
const int kServerRecvBaseTag = 3000;
const int kServerEndBaseTag = 4000;

template <typename F>
auto MakeVec(F init) {
  std::vector<decltype(init(0))> v;
  v.reserve(kNumCalls);
  for (int i = 0; i < kNumCalls; ++i) {
    v.push_back(init(i));
  }
  return v;
}

CORE_END2END_TEST(ResourceQuotaTest, ResourceQuota) {
  if (IsEventEngineListenerEnabled()) {
    GTEST_SKIP() << "Not with event engine listener";
  }

  grpc_resource_quota* resource_quota =
      grpc_resource_quota_create("test_server");
  grpc_resource_quota_resize(resource_quota, 1024 * 1024);
  InitServer(ChannelArgs().Set(
      GRPC_ARG_RESOURCE_QUOTA,
      ChannelArgs::Pointer(resource_quota, grpc_resource_quota_arg_vtable())));
  InitClient(ChannelArgs());
  // Create large request and response bodies. These are big enough to require
  // multiple round trips to deliver to the peer, and their exact contents of
  // will be verified on completion.
  auto requests = MakeVec([](int) { return RandomSlice(128 * 1024); });
  auto server_calls =
      MakeVec([this](int i) { return RequestCall(kServerRecvBaseTag + i); });
  IncomingMetadata server_metadata[kNumCalls];
  IncomingStatusOnClient server_status[kNumCalls];
  IncomingMessage client_message[kNumCalls];
  IncomingCloseOnServer client_close[kNumCalls];
  enum class SeenServerCall {
    kNotSeen = 0,
    kSeenWithSuccess,
    kSeenWithFailure
  };
  // Yep, this really initializes all the elements.
  SeenServerCall seen_server_call[kNumCalls] = {SeenServerCall::kNotSeen};
  auto client_calls =
      MakeVec([this, &requests, &server_metadata, &server_status](int i) {
        auto c = NewClientCall("/foo").Timeout(Duration::Seconds(5)).Create();
        c.NewBatch(kClientBaseTag + i)
            .SendInitialMetadata({}, GRPC_INITIAL_METADATA_WAIT_FOR_READY)
            .SendMessage(requests[i].Ref())
            .SendCloseFromClient()
            .RecvInitialMetadata(server_metadata[i])
            .RecvStatusOnClient(server_status[i]);
        return c;
      });
  for (int i = 0; i < kNumCalls; i++) {
    Expect(kClientBaseTag + i, true);
    Expect(
        kServerRecvBaseTag + i,
        MaybePerformAction{[this, &seen_server_call, &server_calls,
                            &client_message, &client_close, i](bool success) {
          seen_server_call[i] = success ? SeenServerCall::kSeenWithSuccess
                                        : SeenServerCall::kSeenWithFailure;
          if (!success) return;
          server_calls[i]
              .NewBatch(kServerStartBaseTag + i)
              .RecvMessage(client_message[i])
              .SendInitialMetadata({});
          Expect(kServerStartBaseTag + i,
                 PerformAction{[&server_calls, &client_close, i](bool) {
                   server_calls[i]
                       .NewBatch(kServerEndBaseTag + i)
                       .RecvCloseOnServer(client_close[i])
                       .SendStatusFromServer(GRPC_STATUS_OK, "xyz", {});
                 }});
          Expect(kServerEndBaseTag + i, true);
        }});
  }
  Step();

  int cancelled_calls_on_client = 0;
  int cancelled_calls_on_server = 0;
  int deadline_exceeded = 0;
  int unavailable = 0;
  for (int i = 0; i < kNumCalls; i++) {
    switch (server_status[i].status()) {
      case GRPC_STATUS_RESOURCE_EXHAUSTED:
        cancelled_calls_on_client++;
        break;
      case GRPC_STATUS_DEADLINE_EXCEEDED:
        deadline_exceeded++;
        break;
      case GRPC_STATUS_UNAVAILABLE:
        unavailable++;
        break;
      case GRPC_STATUS_OK:
        break;
      default:
        Crash(absl::StrFormat("Unexpected status code: %d",
                              server_status[i].status()));
    }
    if (seen_server_call[i] == SeenServerCall::kSeenWithSuccess &&
        client_close[i].was_cancelled()) {
      cancelled_calls_on_server++;
    }
  }
  gpr_log(GPR_INFO,
          "Done. %d total calls: %d cancelled at server, %d cancelled at "
          "client, %d timed out, %d unavailable.",
          kNumCalls, cancelled_calls_on_server, cancelled_calls_on_client,
          deadline_exceeded, unavailable);

  ShutdownServerAndNotify(0);
  Expect(0, PerformAction{[this](bool success) {
           EXPECT_TRUE(success);
           DestroyServer();
         }});
  for (size_t i = 0; i < kNumCalls; i++) {
    if (seen_server_call[i] == SeenServerCall::kNotSeen) {
      Expect(kServerRecvBaseTag + i, false);
    }
  }
  Step();
}

}  // namespace
}  // namespace grpc_core
