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

#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/status_helper.h"

// These routines are here to facilitate debugging - they produce string
// representations of various transport data structures

std::string grpc_transport_stream_op_batch_string(
    grpc_transport_stream_op_batch* op, bool truncate) {
  std::string out;

  if (op->send_initial_metadata) {
    absl::StrAppend(&out, " SEND_INITIAL_METADATA{");
    if (truncate) {
      absl::StrAppend(&out, "Length=",
                      op->payload->send_initial_metadata.send_initial_metadata
                          ->TransportSize());
    } else {
      absl::StrAppend(&out, op->payload->send_initial_metadata
                                .send_initial_metadata->DebugString());
    }
    absl::StrAppend(&out, "}");
  }

  if (op->send_message) {
    if (op->payload->send_message.send_message != nullptr) {
      absl::StrAppendFormat(&out, " SEND_MESSAGE:flags=0x%08x:len=%d",
                            op->payload->send_message.flags,
                            op->payload->send_message.send_message->Length());
    } else {
      // This can happen when we check a batch after the transport has
      // processed and cleared the send_message op.
      absl::StrAppend(
          &out, " SEND_MESSAGE(flag and length unknown, already orphaned)");
    }
  }

  if (op->send_trailing_metadata) {
    absl::StrAppend(&out, " SEND_TRAILING_METADATA{");
    if (truncate) {
      absl::StrAppend(&out, "Length=",
                      op->payload->send_trailing_metadata
                          .send_trailing_metadata->TransportSize());
    } else {
      absl::StrAppend(&out, op->payload->send_trailing_metadata
                                .send_trailing_metadata->DebugString());
    }
    absl::StrAppend(&out, "}");
  }

  if (op->recv_initial_metadata) {
    absl::StrAppend(&out, " RECV_INITIAL_METADATA");
  }

  if (op->recv_message) {
    absl::StrAppend(&out, " RECV_MESSAGE");
  }

  if (op->recv_trailing_metadata) {
    absl::StrAppend(&out, " RECV_TRAILING_METADATA");
  }

  if (op->cancel_stream) {
    absl::StrAppend(
        &out, " CANCEL:",
        grpc_core::StatusToString(op->payload->cancel_stream.cancel_error));
  }

  return out;
}

std::string grpc_transport_op_string(grpc_transport_op* op) {
  std::string out;

  if (op->start_connectivity_watch != nullptr) {
    absl::StrAppendFormat(
        &out, " START_CONNECTIVITY_WATCH:watcher=%p:from=%s",
        op->start_connectivity_watch.get(),
        grpc_core::ConnectivityStateName(op->start_connectivity_watch_state));
  }

  if (op->stop_connectivity_watch != nullptr) {
    absl::StrAppendFormat(&out, " STOP_CONNECTIVITY_WATCH:watcher=%p",
                          op->stop_connectivity_watch);
  }

  if (!op->disconnect_with_error.ok()) {
    absl::StrAppend(&out, " DISCONNECT:",
                    grpc_core::StatusToString(op->disconnect_with_error));
  }

  if (!op->goaway_error.ok()) {
    absl::StrAppend(
        &out, " SEND_GOAWAY:", grpc_core::StatusToString(op->goaway_error));
  }

  if (op->set_accept_stream) {
    absl::StrAppendFormat(&out, " SET_ACCEPT_STREAM:%p(%p,...)",
                          op->set_accept_stream_fn,
                          op->set_accept_stream_user_data);
  }

  if (op->bind_pollset != nullptr) {
    absl::StrAppend(&out, " BIND_POLLSET");
  }

  if (op->bind_pollset_set != nullptr) {
    absl::StrAppend(&out, " BIND_POLLSET_SET");
  }

  if (op->send_ping.on_initiate != nullptr || op->send_ping.on_ack != nullptr) {
    absl::StrAppend(&out, " SEND_PING");
  }

  return out;
}
