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

#include <memory>

#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/transport/handshaker.h"
#include "src/core/lib/transport/handshaker_factory.h"
#include "src/core/lib/transport/handshaker_registry.h"
#include "test/core/handshake/server_ssl_common.h"
#include "test/core/util/test_config.h"

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
  ~ReadAheadHandshaker() override {}
  const char* name() const override { return "read_ahead"; }
  void Shutdown(grpc_error_handle /*why*/) override {}
  void DoHandshake(grpc_tcp_server_acceptor* /*acceptor*/,
                   grpc_closure* on_handshake_done,
                   HandshakerArgs* args) override {
    grpc_endpoint_read(args->endpoint, args->read_buffer, on_handshake_done,
                       /*urgent=*/false, /*min_progress_size=*/1);
  }
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
