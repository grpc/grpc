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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/util/test_config.h"

void test_unknown_scheme_target(void) {
  grpc_channel* chan;
  /* avoid default prefix */
  grpc_core::ResolverRegistry::Builder::ShutdownRegistry();
  grpc_core::ResolverRegistry::Builder::InitRegistry();

  chan = grpc_insecure_channel_create("blah://blah", nullptr, nullptr);
  GPR_ASSERT(chan != nullptr);

  grpc_core::ExecCtx exec_ctx;
  grpc_channel_element* elem =
      grpc_channel_stack_element(grpc_channel_get_channel_stack(chan), 0);
  GPR_ASSERT(0 == strcmp(elem->filter->name, "lame-client"));

  grpc_channel_destroy(chan);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_unknown_scheme_target();
  grpc_shutdown();
  return 0;
}
