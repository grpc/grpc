/*
 *
 * Copyright 2016 gRPC authors.
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

#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "src/core/lib/channel/handshaker_factory.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/security/transport/security_handshaker.h"

#include "test/core/handshake/server_ssl_common.h"

/* The purpose of this test is to exercise the case when a
 * grpc *security_handshaker* begins its handshake with data already
 * in the read buffer of the handshaker arg. This scenario is created by
 * adding a fake "readahead" handshaker at the beginning of the server's
 * handshaker list, which just reads from the connection and then places
 * read bytes into the read buffer of the handshake arg (to be passed down
 * to the security_handshaker). This test is meant to protect code relying on
 * this functionality that lives outside of this repo. */

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
                       /*urgent=*/false);
  }
};

class ReadAheadHandshakerFactory : public HandshakerFactory {
 public:
  void AddHandshakers(const grpc_channel_args* /*args*/,
                      grpc_pollset_set* /*interested_parties*/,
                      HandshakeManager* handshake_mgr) override {
    handshake_mgr->Add(MakeRefCounted<ReadAheadHandshaker>());
  }
  ~ReadAheadHandshakerFactory() override = default;
};

}  // namespace grpc_core

int main(int /*argc*/, char* /*argv*/[]) {
  grpc_init();
  grpc_core::HandshakerRegistry::RegisterHandshakerFactory(
      true /* at_start */, grpc_core::HANDSHAKER_SERVER,
      absl::make_unique<grpc_core::ReadAheadHandshakerFactory>());
  const char* full_alpn_list[] = {"grpc-exp", "h2"};
  GPR_ASSERT(server_ssl_test(full_alpn_list, 2, "grpc-exp"));
  grpc_shutdown();
  return 0;
}
