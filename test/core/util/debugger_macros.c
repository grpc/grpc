/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * A collection of 'macros' that help navigating the grpc object hierarchy
 * Not intended to be robust for main-line code, often cuts across abstraction
 * boundaries.
 */

#include <stdio.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/transport/chttp2/transport/internal.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/surface/call.h"

void grpc_summon_debugger_macros() {}

grpc_stream *grpc_transport_stream_from_call(grpc_call *call) {
  grpc_call_stack *cs = grpc_call_get_call_stack(call);
  for (;;) {
    grpc_call_element *el = grpc_call_stack_element(cs, cs->count - 1);
    if (el->filter == &grpc_client_channel_filter) {
      grpc_subchannel_call *scc = grpc_client_channel_get_subchannel_call(el);
      if (scc == NULL) {
        fprintf(stderr, "No subchannel-call");
        return NULL;
      }
      cs = grpc_subchannel_call_get_call_stack(scc);
    } else if (el->filter == &grpc_connected_filter) {
      return grpc_connected_channel_get_stream(el);
    } else {
      fprintf(stderr, "Unrecognized filter: %s", el->filter->name);
      return NULL;
    }
  }
}

grpc_chttp2_stream *grpc_chttp2_stream_from_call(grpc_call *call) {
  return (grpc_chttp2_stream *)grpc_transport_stream_from_call(call);
}
