/*
 *
 * Copyright 2017, Google Inc.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H
#define GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H

#include <grpc/support/port_platform.h>
#include "src/core/ext/transport/chttp2/transport/internal.h"

#include <stddef.h>

/* module that handles all flow control related logic */

typedef enum {
  // Nothing to be done.
  GRPC_CHTTP2_FLOW_CONTROL_NO_ACTION_NEEDED = 0,
  // Initiate a write to update the initial window immediately.
  GRPC_CHTTP2_FLOW_CONTROL_UPDATE_IMMEDIATELY,
  // Update withing 100 milliseconds. TODO(ncteisen): tune this
  GRPC_CHTTP2_FLOW_CONTROL_UPDATE_SOON,
  // Push the flow control update into a send buffer, to be sent
  // out the next time a write is initiated.
  GRPC_CHTTP2_FLOW_CONTROL_QUEUE_UPDATE,
} grpc_chttp2_flow_control_urgency;

typedef struct {
  grpc_chttp2_flow_control_urgency send_bdp_ping;
  grpc_chttp2_flow_control_urgency send_stream_update;
  grpc_chttp2_flow_control_urgency send_transport_update;
  grpc_chttp2_flow_control_urgency send_max_frame_update;
  int64_t announce_transport_window;
  int64_t announce_stream_window;
  int64_t announce_max_frame;
} grpc_chttp2_flow_control_action;

grpc_chttp2_flow_control_action grpc_chttp2_check_for_flow_control_action(
    grpc_chttp2_transport* t, grpc_chttp2_stream* s);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H */
