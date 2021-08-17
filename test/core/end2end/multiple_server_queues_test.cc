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

#include <grpc/grpc.h>
#include "test/core/util/test_config.h"

int main(int argc, char** argv) {
  grpc_completion_queue* cq1;
  grpc_completion_queue* cq2;
  grpc_completion_queue* cq3;
  grpc_completion_queue_attributes attr;

  grpc_server* server;

  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();

  attr.version = 1;
  attr.cq_completion_type = GRPC_CQ_NEXT;
  attr.cq_polling_type = GRPC_CQ_DEFAULT_POLLING;
  cq1 = grpc_completion_queue_create(
      grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

  attr.cq_polling_type = GRPC_CQ_NON_LISTENING;
  cq2 = grpc_completion_queue_create(
      grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

  attr.cq_polling_type = GRPC_CQ_NON_POLLING;
  cq3 = grpc_completion_queue_create(
      grpc_completion_queue_factory_lookup(&attr), &attr, nullptr);

  server = grpc_server_create(nullptr, nullptr);
  grpc_server_register_completion_queue(server, cq1, nullptr);
  grpc_server_add_insecure_http2_port(server, "[::]:0");
  grpc_server_register_completion_queue(server, cq2, nullptr);
  grpc_server_register_completion_queue(server, cq3, nullptr);

  grpc_server_start(server);
  grpc_server_shutdown_and_notify(server, cq2, nullptr);
  grpc_completion_queue_next(cq2, gpr_inf_future(GPR_CLOCK_REALTIME),
                             nullptr); /* cue queue freeze */
  grpc_completion_queue_shutdown(cq1);
  grpc_completion_queue_shutdown(cq2);
  grpc_completion_queue_shutdown(cq3);

  grpc_completion_queue_next(cq1, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  grpc_completion_queue_next(cq2, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
  grpc_completion_queue_next(cq3, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);

  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq1);
  grpc_completion_queue_destroy(cq2);
  grpc_completion_queue_destroy(cq3);
  grpc_shutdown();
  return 0;
}
