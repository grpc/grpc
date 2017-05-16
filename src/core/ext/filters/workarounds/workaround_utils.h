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

#ifndef GRPC_CORE_EXT_FILTERS_WORKAROUNDS_WORKAROUND_UTILS_H
#define GRPC_CORE_EXT_FILTERS_WORKAROUNDS_WORKAROUND_UTILS_H

#include <grpc/support/workaround_list.h>

#include "src/core/lib/transport/metadata.h"

#define GRPC_WORKAROUND_PRIORITY_HIGH 10001
#define GRPC_WORKAROUND_PROIRITY_LOW 9999

typedef struct grpc_workaround_user_agent_md {
  bool workaround_active[GRPC_MAX_WORKAROUND_ID];
} grpc_workaround_user_agent_md;

grpc_workaround_user_agent_md *grpc_parse_user_agent(grpc_mdelem md);

typedef bool (*user_agent_parser)(grpc_mdelem);

void grpc_register_workaround(uint32_t id, user_agent_parser parser);

#endif
