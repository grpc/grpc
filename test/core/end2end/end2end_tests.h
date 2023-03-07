//
//
// Copyright 2015 gRPC authors.
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

#ifndef GRPC_TEST_CORE_END2END_END2END_TESTS_H
#define GRPC_TEST_CORE_END2END_END2END_TESTS_H

#include <stdint.h>

#include <functional>
#include <memory>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/util/test_config.h"

// Test feature flags.
#define FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION 1
#define FEATURE_MASK_SUPPORTS_HOSTNAME_VERIFICATION 2
// Feature mask supports call credentials with a minimum security level of
// GRPC_PRIVACY_AND_INTEGRITY.
#define FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS 4
// Feature mask supports call credentials with a minimum security level of
// GRPC_SECURTITY_NONE.
#define FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS_LEVEL_INSECURE 8
#define FEATURE_MASK_SUPPORTS_REQUEST_PROXYING 16
#define FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL 32
#define FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER 64
#define FEATURE_MASK_DOES_NOT_SUPPORT_CLIENT_HANDSHAKE_COMPLETE_FIRST 1024
#define FEATURE_MASK_DOES_NOT_SUPPORT_DEADLINES 2048

#define FAIL_AUTH_CHECK_SERVER_ARG_NAME "fail_auth_check"

class CoreTestFixture {
 public:
  virtual ~CoreTestFixture() {
    ShutdownServer();
    ShutdownClient();
    grpc_completion_queue_shutdown(cq());
    DrainCq();
    grpc_completion_queue_destroy(cq());
  }

  grpc_completion_queue* cq() { return cq_; }
  grpc_server* server() { return server_; }
  grpc_channel* client() { return client_; }

  void InitServer(const grpc_core::ChannelArgs& args) {
    if (server_ != nullptr) ShutdownServer();
    server_ = MakeServer(args);
    GPR_ASSERT(server_ != nullptr);
  }
  void InitClient(const grpc_core::ChannelArgs& args) {
    if (client_ != nullptr) ShutdownClient();
    client_ = MakeClient(args);
    GPR_ASSERT(client_ != nullptr);
  }

  void ShutdownServer() {
    if (server_ == nullptr) return;
    grpc_server_shutdown_and_notify(server_, cq_, server_);
    grpc_event ev;
    do {
      ev = grpc_completion_queue_next(cq_, grpc_timeout_seconds_to_deadline(5),
                                      nullptr);
    } while (ev.type != GRPC_OP_COMPLETE || ev.tag != server_);
    DestroyServer();
  }

  void DestroyServer() {
    if (server_ == nullptr) return;
    grpc_server_destroy(server_);
    server_ = nullptr;
  }

  void ShutdownClient() {
    if (client_ == nullptr) return;
    grpc_channel_destroy(client_);
    client_ = nullptr;
  }

 protected:
  void SetServer(grpc_server* server);
  void SetClient(grpc_channel* client);

 private:
  virtual grpc_server* MakeServer(const grpc_core::ChannelArgs& args) = 0;
  virtual grpc_channel* MakeClient(const grpc_core::ChannelArgs& args) = 0;

  void DrainCq() {
    grpc_event ev;
    do {
      ev = grpc_completion_queue_next(cq_, grpc_timeout_seconds_to_deadline(5),
                                      nullptr);
    } while (ev.type != GRPC_QUEUE_SHUTDOWN);
  }

  grpc_completion_queue* cq_ = grpc_completion_queue_create_for_next(nullptr);
  grpc_server* server_ = nullptr;
  grpc_channel* client_ = nullptr;
};

struct CoreTestConfiguration {
  // A descriptive name for this test fixture.
  const char* name;

  // Which features are supported by this fixture. See feature flags above.
  uint32_t feature_mask;

  // If the call host is setup by the fixture (for example, via the
  // GRPC_SSL_TARGET_NAME_OVERRIDE_ARG channel arg), which value should the
  // test expect to find in call_details.host
  const char* overridden_call_host;

  std::function<std::unique_ptr<CoreTestFixture>(
      const grpc_core::ChannelArgs& client_args,
      const grpc_core::ChannelArgs& server_args)>
      create_fixture;
};

void grpc_end2end_tests_pre_init(void);
void grpc_end2end_tests(int argc, char** argv,
                        const CoreTestConfiguration& config);

const char* get_host_override_string(const char* str,
                                     const CoreTestConfiguration& config);
// Returns a pointer to a statically allocated slice: future invocations
// overwrite past invocations, not threadsafe, etc...
const grpc_slice* get_host_override_slice(const char* str,
                                          const CoreTestConfiguration& config);

void validate_host_override_string(const char* pattern, grpc_slice str,
                                   const CoreTestConfiguration& config);

#endif  // GRPC_TEST_CORE_END2END_END2END_TESTS_H
