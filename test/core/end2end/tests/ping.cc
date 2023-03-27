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
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/end2end/cq_verifier.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/util/test_config.h"

#define PING_NUM 5

static void test_ping(const CoreTestConfiguration& config,
                      int min_time_between_pings_ms) {
  auto f =
      config.create_fixture(grpc_core::ChannelArgs(), grpc_core::ChannelArgs());
  grpc_core::CqVerifier cqv(f->cq());
  grpc_connectivity_state state = GRPC_CHANNEL_IDLE;
  int i;

  auto client_args = grpc_core::ChannelArgs()
                         .Set(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0)
                         .Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
  auto server_args =
      grpc_core::ChannelArgs()
          .Set(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 0)
          .Set(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);

  f->InitClient(client_args);
  f->InitServer(server_args);

  grpc_channel_ping(f->client(), f->cq(), grpc_core::CqVerifier::tag(0),
                    nullptr);
  cqv.Expect(grpc_core::CqVerifier::tag(0), false);

  // check that we're still in idle, and start connecting
  GPR_ASSERT(grpc_channel_check_connectivity_state(f->client(), 1) ==
             GRPC_CHANNEL_IDLE);
  // we'll go through some set of transitions (some might be missed), until
  // READY is reached
  while (state != GRPC_CHANNEL_READY) {
    grpc_channel_watch_connectivity_state(
        f->client(), state,
        gpr_time_add(grpc_timeout_seconds_to_deadline(3),
                     gpr_time_from_millis(min_time_between_pings_ms * PING_NUM,
                                          GPR_TIMESPAN)),
        f->cq(), grpc_core::CqVerifier::tag(99));
    cqv.Expect(grpc_core::CqVerifier::tag(99), true);
    cqv.Verify();
    state = grpc_channel_check_connectivity_state(f->client(), 0);
    GPR_ASSERT(state == GRPC_CHANNEL_READY ||
               state == GRPC_CHANNEL_CONNECTING ||
               state == GRPC_CHANNEL_TRANSIENT_FAILURE);
  }

  for (i = 1; i <= PING_NUM; i++) {
    grpc_channel_ping(f->client(), f->cq(), grpc_core::CqVerifier::tag(i),
                      nullptr);
    cqv.Expect(grpc_core::CqVerifier::tag(i), true);
    cqv.Verify();
  }

  grpc_server_shutdown_and_notify(f->server(), f->cq(),
                                  grpc_core::CqVerifier::tag(0xdead));
  cqv.Expect(grpc_core::CqVerifier::tag(0xdead), true);
  cqv.Verify();
}

void ping(const CoreTestConfiguration& config) {
  GPR_ASSERT(config.feature_mask & FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION);
  test_ping(config, 0);
  test_ping(config, 100);
}

void ping_pre_init(void) {}
