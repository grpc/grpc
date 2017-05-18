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
  // No action needs to be taken to change flow control.
  FLOW_CONTROL_UPDATE_NONE,
  // Send a new bdp ping.
  FLOW_CONTROL_SEND_BDP_PING,
  // Set the initial window to `value`.
  FLOW_CONTROL_UPDATE_ABSOLUTE,
  // Increment the initial window by `value`.
  FLOW_CONTROL_UPDATE_DELTA,
} flow_control_action_type;

typedef enum {
  // Initiate a write to update the initial window immediately.
  FLOW_CONTROL_UPDATE_IMMEDIATELY,
  // Push the flow control update into a send buffer, to be sent
  // out the next time a write is initiated.
  FLOW_CONTROL_QUEUE_UPDATE,
} flow_control_urgency;

typedef struct {
  flow_control_action_type action_type;
  flow_control_urgency urgency;
  uint64_t value;
} flow_control_action;

flow_control_action check_for_flow_control_action(grpc_chttp2_transport* t,
                                                  grpc_chttp2_stream* s);

#endif /* GRPC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_FLOW_CONTROL_H */
