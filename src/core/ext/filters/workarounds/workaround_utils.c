//
// Copyright 2017, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
  user_agent_md = gpr_malloc(sizeof(grpc_workaround_user_agent_md));
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
