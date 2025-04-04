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

#include <grpc/grpc.h>

#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "src/core/config/core_configuration.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_factory.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "test/core/handshake/server_ssl_common.h"
#include "test/core/test_util/test_config.h"

// The purpose of this test is to exercise the case when a
// grpc *security_handshaker* begins its handshake with data already
// in the read buffer of the handshaker arg. This scenario is created by
// adding a fake "readahead" handshaker at the beginning of the server's
// handshaker list, which just reads from the connection and then places
// read bytes into the read buffer of the handshake arg (to be passed down
// to the security_handshaker). This test is meant to protect code relying on
// this functionality that lives outside of this repo.

namespace grpc_core {

class ReadAheadHandshaker : public Handshaker {
 public:
  absl::string_view name() const override { return "read_ahead"; }

  void DoHandshake(
      HandshakerArgs* args,
      absl::AnyInvocable<void(absl::Status)> on_handshake_done) override {
    MutexLock lock(&mu_);
    args_ = args;
    on_handshake_done_ = std::move(on_handshake_done);
    Ref().release();  // Held by callback.
    GRPC_CLOSURE_INIT(&on_read_done_, OnReadDone, this, nullptr);
    grpc_endpoint_read(args->endpoint.get(), args->read_buffer.c_slice_buffer(),
                       &on_read_done_, /*urgent=*/false,
                       /*min_progress_size=*/1);
  }

  void Shutdown(absl::Status /*error*/) override {
    MutexLock lock(&mu_);
    if (on_handshake_done_ != nullptr) args_->endpoint.reset();
  }

 private:
  static void OnReadDone(void* arg, grpc_error_handle error) {
    auto* self = static_cast<ReadAheadHandshaker*>(arg);
    // Need an async hop here, because grpc_endpoint_read() may invoke
    // the callback synchronously, leading to deadlock.
    // TODO(roth): This async hop will no longer be necessary once we
    // switch to the EventEngine endpoint API.
    self->args_->event_engine->Run(
        [self = RefCountedPtr<ReadAheadHandshaker>(self),
         error = std::move(error)]() mutable {
          absl::AnyInvocable<void(absl::Status)> on_handshake_done;
          {
            MutexLock lock(&self->mu_);
            on_handshake_done = std::move(self->on_handshake_done_);
          }
          on_handshake_done(std::move(error));
        });
  }

  grpc_closure on_read_done_;

  Mutex mu_;
  // Mutex guards args_->endpoint but not the rest of the struct.
  HandshakerArgs* args_ = nullptr;
  absl::AnyInvocable<void(absl::Status)> on_handshake_done_
      ABSL_GUARDED_BY(&mu_);
};

class ReadAheadHandshakerFactory : public HandshakerFactory {
 public:
  void AddHandshakers(const ChannelArgs& /*args*/,
                      grpc_pollset_set* /*interested_parties*/,
                      HandshakeManager* handshake_mgr) override {
    handshake_mgr->Add(MakeRefCounted<ReadAheadHandshaker>());
  }
  HandshakerPriority Priority() override {
    return HandshakerPriority::kReadAheadSecurityHandshakers;
  }
  ~ReadAheadHandshakerFactory() override = default;
};

}  // namespace grpc_core

TEST(HandshakeServerWithReadaheadHandshakerTest, MainTest) {
  grpc_core::CoreConfiguration::WithSubstituteBuilder builder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        BuildCoreConfiguration(builder);
        builder->handshaker_registry()->RegisterHandshakerFactory(
            grpc_core::HANDSHAKER_SERVER,
            std::make_unique<grpc_core::ReadAheadHandshakerFactory>());
      });

  grpc_init();
  const char* full_alpn_list[] = {"h2"};
  ASSERT_TRUE(server_ssl_test(full_alpn_list, 1, "h2"));
  CleanupSslLibrary();
  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
