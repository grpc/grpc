//
// Copyright 2017 gRPC authors.
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

#include "src/core/ext/filters/workarounds/workaround_utils.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

user_agent_parser ua_parser[GRPC_MAX_WORKAROUND_ID];

static void destroy_user_agent_md(void *user_agent_md) {
  gpr_free(user_agent_md);
}

grpc_workaround_user_agent_md *grpc_parse_user_agent(grpc_mdelem md) {
  grpc_workaround_user_agent_md *user_agent_md =
      (grpc_workaround_user_agent_md *)grpc_mdelem_get_user_data(
          md, destroy_user_agent_md);

  if (NULL != user_agent_md) {
    return user_agent_md;
  }
  user_agent_md = (grpc_workaround_user_agent_md *)gpr_malloc(
      sizeof(grpc_workaround_user_agent_md));
  for (int i = 0; i < GRPC_MAX_WORKAROUND_ID; i++) {
    if (ua_parser[i]) {
      user_agent_md->workaround_active[i] = ua_parser[i](md);
    }
  }
  grpc_mdelem_set_user_data(md, destroy_user_agent_md, (void *)user_agent_md);

  return user_agent_md;
}

void grpc_register_workaround(uint32_t id, user_agent_parser parser) {
  GPR_ASSERT(id < GRPC_MAX_WORKAROUND_ID);
  ua_parser[id] = parser;
}
