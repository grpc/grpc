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

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/transport.h"

/* These routines are here to facilitate debugging - they produce string
   representations of various transport data structures */

std::string grpc_transport_stream_op_batch_string(
    grpc_transport_stream_op_batch* op) {
  std::vector<std::string> out;

  if (op->send_initial_metadata) {
    out.push_back(" SEND_INITIAL_METADATA{");
    out.push_back(op->payload->send_initial_metadata.send_initial_metadata
                      ->DebugString());
    out.push_back("}");
  }

  if (op->send_message) {
    if (op->payload->send_message.send_message != nullptr) {
      out.push_back(absl::StrFormat(
          " SEND_MESSAGE:flags=0x%08x:len=%d", op->payload->send_message.flags,
          op->payload->send_message.send_message->Length()));
    } else {
      // This can happen when we check a batch after the transport has
      // processed and cleared the send_message op.
      out.push_back(" SEND_MESSAGE(flag and length unknown, already orphaned)");
    }
  }

  if (op->send_trailing_metadata) {
    out.push_back(" SEND_TRAILING_METADATA{");
    out.push_back(op->payload->send_trailing_metadata.send_trailing_metadata
                      ->DebugString());
    out.push_back("}");
  }

  if (op->recv_initial_metadata) {
    out.push_back(" RECV_INITIAL_METADATA");
  }

  if (op->recv_message) {
    out.push_back(" RECV_MESSAGE");
  }

  if (op->recv_trailing_metadata) {
    out.push_back(" RECV_TRAILING_METADATA");
  }

  if (op->cancel_stream) {
    out.push_back(absl::StrCat(
        " CANCEL:",
        grpc_core::StatusToString(op->payload->cancel_stream.cancel_error)));
  }

  return absl::StrJoin(out, "");
}

std::string grpc_transport_op_string(grpc_transport_op* op) {
  std::vector<std::string> out;

  if (op->start_connectivity_watch != nullptr) {
    out.push_back(absl::StrFormat(
        " START_CONNECTIVITY_WATCH:watcher=%p:from=%s",
        op->start_connectivity_watch.get(),
        grpc_core::ConnectivityStateName(op->start_connectivity_watch_state)));
  }

  if (op->stop_connectivity_watch != nullptr) {
    out.push_back(absl::StrFormat(" STOP_CONNECTIVITY_WATCH:watcher=%p",
                                  op->stop_connectivity_watch));
  }

  if (!op->disconnect_with_error.ok()) {
    out.push_back(absl::StrCat(
        " DISCONNECT:", grpc_core::StatusToString(op->disconnect_with_error)));
  }

  if (!op->goaway_error.ok()) {
    out.push_back(absl::StrCat(" SEND_GOAWAY:",
                               grpc_core::StatusToString(op->goaway_error)));
  }

  if (op->set_accept_stream) {
    out.push_back(absl::StrFormat(" SET_ACCEPT_STREAM:%p(%p,...)",
                                  op->set_accept_stream_fn,
                                  op->set_accept_stream_user_data));
  }

  if (op->bind_pollset != nullptr) {
    out.push_back(" BIND_POLLSET");
  }

  if (op->bind_pollset_set != nullptr) {
    out.push_back(" BIND_POLLSET_SET");
  }

  if (op->send_ping.on_initiate != nullptr || op->send_ping.on_ack != nullptr) {
    out.push_back(" SEND_PING");
  }

  return absl::StrJoin(out, "");
}

void grpc_call_log_op(const char* file, int line, gpr_log_severity severity,
                      grpc_call_element* elem,
                      grpc_transport_stream_op_batch* op) {
  gpr_log(file, line, severity, "OP[%s:%p]: %s", elem->filter->name, elem,
          grpc_transport_stream_op_batch_string(op).c_str());
}
