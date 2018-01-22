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

#include "src/core/lib/surface/call.h"

#include <inttypes.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/slice/slice_string_helpers.h"

static void add_metadata(gpr_strvec* b, const grpc_metadata* md, size_t count) {
  size_t i;
  if (md == nullptr) {
    gpr_strvec_add(b, gpr_strdup("(nil)"));
    return;
  }
  for (i = 0; i < count; i++) {
    gpr_strvec_add(b, gpr_strdup("\nkey="));
    gpr_strvec_add(b, grpc_slice_to_c_string(md[i].key));

    gpr_strvec_add(b, gpr_strdup(" value="));
    gpr_strvec_add(b,
                   grpc_dump_slice(md[i].value, GPR_DUMP_HEX | GPR_DUMP_ASCII));
  }
}

char* grpc_op_string(const grpc_op* op) {
  char* tmp;
  char* out;

  gpr_strvec b;
  gpr_strvec_init(&b);

  switch (op->op) {
    case GRPC_OP_SEND_INITIAL_METADATA:
      gpr_strvec_add(&b, gpr_strdup("SEND_INITIAL_METADATA"));
      add_metadata(&b, op->data.send_initial_metadata.metadata,
                   op->data.send_initial_metadata.count);
      break;
    case GRPC_OP_SEND_MESSAGE:
      gpr_asprintf(&tmp, "SEND_MESSAGE ptr=%p",
                   op->data.send_message.send_message);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
      gpr_strvec_add(&b, gpr_strdup("SEND_CLOSE_FROM_CLIENT"));
      break;
    case GRPC_OP_SEND_STATUS_FROM_SERVER:
      gpr_asprintf(&tmp, "SEND_STATUS_FROM_SERVER status=%d details=",
                   op->data.send_status_from_server.status);
      gpr_strvec_add(&b, tmp);
      if (op->data.send_status_from_server.status_details != nullptr) {
        gpr_strvec_add(&b, grpc_dump_slice(
                               *op->data.send_status_from_server.status_details,
                               GPR_DUMP_ASCII));
      } else {
        gpr_strvec_add(&b, gpr_strdup("(null)"));
      }
      add_metadata(&b, op->data.send_status_from_server.trailing_metadata,
                   op->data.send_status_from_server.trailing_metadata_count);
      break;
    case GRPC_OP_RECV_INITIAL_METADATA:
      gpr_asprintf(&tmp, "RECV_INITIAL_METADATA ptr=%p",
                   op->data.recv_initial_metadata.recv_initial_metadata);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_OP_RECV_MESSAGE:
      gpr_asprintf(&tmp, "RECV_MESSAGE ptr=%p",
                   op->data.recv_message.recv_message);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_OP_RECV_STATUS_ON_CLIENT:
      gpr_asprintf(&tmp,
                   "RECV_STATUS_ON_CLIENT metadata=%p status=%p details=%p",
                   op->data.recv_status_on_client.trailing_metadata,
                   op->data.recv_status_on_client.status,
                   op->data.recv_status_on_client.status_details);
      gpr_strvec_add(&b, tmp);
      break;
    case GRPC_OP_RECV_CLOSE_ON_SERVER:
      gpr_asprintf(&tmp, "RECV_CLOSE_ON_SERVER cancelled=%p",
                   op->data.recv_close_on_server.cancelled);
      gpr_strvec_add(&b, tmp);
  }
  out = gpr_strvec_flatten(&b, nullptr);
  gpr_strvec_destroy(&b);

  return out;
}

void grpc_call_log_batch(const char* file, int line, gpr_log_severity severity,
                         grpc_call* call, const grpc_op* ops, size_t nops,
                         void* tag) {
  char* tmp;
  size_t i;
  for (i = 0; i < nops; i++) {
    tmp = grpc_op_string(&ops[i]);
    gpr_log(file, line, severity, "ops[%" PRIuPTR "]: %s", i, tmp);
    gpr_free(tmp);
  }
}
