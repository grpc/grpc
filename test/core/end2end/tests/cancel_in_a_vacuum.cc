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

#include <functional>
#include <memory>

#include <grpc/grpc.h>
#include <grpc/impl/propagation_bits.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/tests/cancel_test_helpers.h"
#include "test/core/util/test_config.h"

static std::unique_ptr<CoreTestFixture> begin_test(
    const CoreTestConfiguration& config, const char* test_name,
    cancellation_mode mode, grpc_channel_args* client_args,
    grpc_channel_args* server_args) {
  gpr_log(GPR_INFO, "Running test: %s/%s/%s", test_name, config.name,
          mode.name);
  auto f = config.create_fixture(grpc_core::ChannelArgs::FromC(client_args),
                                 grpc_core::ChannelArgs::FromC(server_args));
  f->InitServer(grpc_core::ChannelArgs::FromC(server_args));
  f->InitClient(grpc_core::ChannelArgs::FromC(client_args));
  return f;
}

// Cancel and do nothing
static void test_cancel_in_a_vacuum(const CoreTestConfiguration& config,
                                    cancellation_mode mode) {
  grpc_call* c;
  auto f =
      begin_test(config, "test_cancel_in_a_vacuum", mode, nullptr, nullptr);

  gpr_timespec deadline = grpc_timeout_seconds_to_deadline(5);
  c = grpc_channel_create_call(f->client(), nullptr, GRPC_PROPAGATE_DEFAULTS,
                               f->cq(), grpc_slice_from_static_string("/foo"),
                               nullptr, deadline, nullptr);
  GPR_ASSERT(c);

  GPR_ASSERT(GRPC_CALL_OK == mode.initiate_cancel(c, nullptr));

  grpc_call_unref(c);
}

void cancel_in_a_vacuum(const CoreTestConfiguration& config) {
  unsigned i;

  for (i = 0; i < GPR_ARRAY_SIZE(cancellation_modes); i++) {
    test_cancel_in_a_vacuum(config, cancellation_modes[i]);
  }
}

void cancel_in_a_vacuum_pre_init(void) {}
