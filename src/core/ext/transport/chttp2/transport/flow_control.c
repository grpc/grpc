/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/flow_control.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include "src/core/lib/support/string.h"

grpc_tracer_flag grpc_flowctl_trace = GRPC_TRACER_INITIALIZER(false);

#define DEFAULT_WINDOW 65535;

typedef enum {
  REMOTE,
  LOCAL,
  ANNOUNCE,
} flow_control_bookkeeping_type;

typedef enum {
  TRANSPORT,
  STREAM,
} flow_control_transport_or_stream;

// This is the common function to manipulate flow control. It all happens here
// so we can a achieve a standard tracing in one place.
static void manipulate_flow_control_common(
    grpc_chttp2_transport_flow_control_data *tfc,
    grpc_chttp2_stream_flow_control_data *sfc,
    flow_control_bookkeeping_type type, flow_control_transport_or_stream tos,
    int64_t delta) {

  if (GRPC_TRACER_ON(grpc_flowctl_trace)) {
    // TODO
  }

  switch (tos) {
    case TRANSPORT:
      GPR_ASSERT(tfc);
      switch (type) {
        case REMOTE:
          tfc->remote_window += delta;
          break;
        case LOCAL:
          tfc->local_window += delta;
          break;
        case ANNOUNCE:
          tfc->announced_local_window += delta;
          break;
        default:
          GPR_UNREACHABLE_CODE(return );
      }
      break;
    case STREAM:
      GPR_ASSERT(sfc);
      switch (type) {
        case REMOTE:
          sfc->remote_window_delta += delta;
          break;
        case LOCAL:
          sfc->local_window_delta += delta;
          break;
        case ANNOUNCE:
          sfc->announced_local_window_delta += delta;
          break;
        default:
          GPR_UNREACHABLE_CODE(return );
      }
      break;
    default:
      GPR_UNREACHABLE_CODE(return );
  }
}

/*******************************************************************************
 * INTERFACE
 */

void grpc_chttp2_flow_control_init(
    grpc_chttp2_transport_flow_control_data *tfc) {
  tfc->remote_window = DEFAULT_WINDOW;
  tfc->local_window = DEFAULT_WINDOW;
}

void grpc_chttp2_flow_control_credit_local_transport(
    grpc_chttp2_transport_flow_control_data *tfc, int64_t val) {
  manipulate_flow_control_common(tfc, NULL, LOCAL, TRANSPORT, val);
}

void grpc_chttp2_flow_control_debit_local_transport(
    grpc_chttp2_transport_flow_control_data *tfc, int64_t val) {
  manipulate_flow_control_common(tfc, NULL, LOCAL, TRANSPORT, -val);
}

void grpc_chttp2_flow_control_credit_local_stream(
    grpc_chttp2_stream_flow_control_data *sfc, int64_t val) {
  manipulate_flow_control_common(NULL, sfc, LOCAL, STREAM, val);
}

void grpc_chttp2_flow_control_debit_local_stream(
    grpc_chttp2_stream_flow_control_data *sfc, int64_t val) {
  manipulate_flow_control_common(NULL, sfc, LOCAL, STREAM, -val);
}

void grpc_chttp2_flow_control_announce_credit_transport(
    grpc_chttp2_transport_flow_control_data *tfc, int64_t val) {
  manipulate_flow_control_common(tfc, NULL, ANNOUNCE, TRANSPORT, val);
}

void grpc_chttp2_flow_control_announce_debit_transport(
    grpc_chttp2_transport_flow_control_data *tfc, int64_t val) {
  manipulate_flow_control_common(tfc, NULL, ANNOUNCE, TRANSPORT, -val);
}

void grpc_chttp2_flow_control_announce_credit_stream(
    grpc_chttp2_stream_flow_control_data *sfc, int64_t val) {
  manipulate_flow_control_common(NULL, sfc, ANNOUNCE, STREAM, val);
}

void grpc_chttp2_flow_control_announce_debit_stream(
    grpc_chttp2_stream_flow_control_data *sfc, int64_t val) {
  manipulate_flow_control_common(NULL, sfc, ANNOUNCE, STREAM, -val);
}

void grpc_chttp2_flow_control_credit_remote_transport(
    grpc_chttp2_transport_flow_control_data *tfc, int64_t val) {
  manipulate_flow_control_common(tfc, NULL, REMOTE, TRANSPORT, val);
}

void grpc_chttp2_flow_control_debit_remote_transport(
    grpc_chttp2_transport_flow_control_data *tfc, int64_t val) {
  manipulate_flow_control_common(tfc, NULL, REMOTE, TRANSPORT, -val);
}

void grpc_chttp2_flow_control_credit_remote_stream(
    grpc_chttp2_stream_flow_control_data *sfc, int64_t val) {
  manipulate_flow_control_common(NULL, sfc, REMOTE, STREAM, val);
}

void grpc_chttp2_flow_control_debit_remote_stream(
    grpc_chttp2_stream_flow_control_data *sfc, int64_t val) {
  manipulate_flow_control_common(NULL, sfc, REMOTE, STREAM, -val);
}

/*******************************************************************************
 * TRACING
 */

static char *format_flowctl_context_var(const char *context, const char *var,
                                        int64_t val, uint32_t id) {
  char *name;
  if (context == NULL) {
    name = gpr_strdup(var);
  } else if (0 == strcmp(context, "t")) {
    GPR_ASSERT(id == 0);
    gpr_asprintf(&name, "TRANSPORT:%s", var);
  } else if (0 == strcmp(context, "s")) {
    GPR_ASSERT(id != 0);
    gpr_asprintf(&name, "STREAM[%d]:%s", id, var);
  } else {
    gpr_asprintf(&name, "BAD_CONTEXT[%s][%d]:%s", context, id, var);
  }
  char *name_fld = gpr_leftpad(name, ' ', 64);
  char *value;
  gpr_asprintf(&value, "%" PRId64, val);
  char *value_fld = gpr_leftpad(value, ' ', 8);
  char *result;
  gpr_asprintf(&result, "%s %s", name_fld, value_fld);
  gpr_free(name);
  gpr_free(name_fld);
  gpr_free(value);
  gpr_free(value_fld);
  return result;
}

void grpc_chttp2_flowctl_trace(const char *file, int line, const char *phase,
                               grpc_chttp2_flowctl_op op, const char *context1,
                               const char *var1, const char *context2,
                               const char *var2, int is_client,
                               uint32_t stream_id, int64_t val1, int64_t val2) {
  char *tmp_phase;
  char *label1 = format_flowctl_context_var(context1, var1, val1, stream_id);
  char *label2 = format_flowctl_context_var(context2, var2, val2, stream_id);
  char *clisvr = is_client ? "client" : "server";
  char *prefix;

  tmp_phase = gpr_leftpad(phase, ' ', 8);
  gpr_asprintf(&prefix, "FLOW %s: %s ", tmp_phase, clisvr);
  gpr_free(tmp_phase);

  switch (op) {
    case GRPC_CHTTP2_FLOWCTL_MOVE:
      if (val2 != 0) {
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "%sMOVE   %s <- %s giving %" PRId64, prefix, label1, label2,
                val1 + val2);
      }
      break;
    case GRPC_CHTTP2_FLOWCTL_CREDIT:
      GPR_ASSERT(val2 >= 0);
      if (val2 != 0) {
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "%sCREDIT %s by %s giving %" PRId64, prefix, label1, label2,
                val1 + val2);
      }
      break;
    case GRPC_CHTTP2_FLOWCTL_DEBIT:
      GPR_ASSERT(val2 >= 0);
      if (val2 != 0) {
        gpr_log(file, line, GPR_LOG_SEVERITY_DEBUG,
                "%sDEBIT  %s by %s giving %" PRId64, prefix, label1, label2,
                val1 - val2);
      }
      break;
  }

  gpr_free(label1);
  gpr_free(label2);
  gpr_free(prefix);
}
