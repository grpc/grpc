/*
 *
 * Copyright 2016 gRPC authors.
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

#include <ctype.h>
#include <string.h>

#include <grpc/grpc_security.h>
#include <grpc/load_reporting.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/uri_parser.h"
#include "src/core/ext/filters/load_reporting/registered_opencensus_objects.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_posix.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/transport/static_metadata.h"

namespace {

constexpr char kEncodedIpv4AddressLengthString[] = "08";
constexpr char kEncodedIpv6AddressLengthString[] = "32";
constexpr char kEmptyAddressLengthString[] = "00";
constexpr size_t kLengthPrefixSize = 2;

typedef struct call_data {
  // The peer string (a member of the recv_initial_metadata op). Note that
  // gpr_atm itself is a pointer type here, making "peer_string" effectively a
  // double pointer.
  gpr_atm* peer_string;

  // The received initial metadata (a member of the recv_initial_metadata op).
  // When it is ready, we will extract some data from it via
  // wrapped_recv_initial_metadata_ready closure, before the original
  // recv_initial_metadata_ready closure,
  grpc_metadata_batch* recv_initial_metadata;

  // The original recv_initial_metadata closure, which is wrapped by our own
  // closure (wrapped_recv_initial_metadata_ready) to capture the incoming
  // initial metadata.
  grpc_closure* original_recv_initial_metadata_ready;

  // The closure that wraps the original closure. Scheduled when
  // recv_initial_metadata is ready.
  grpc_closure wrapped_recv_initial_metadata_ready;

  // Corresponds to the :path header.
  // TODO(bdrutu): Can we use the grpc_call_element_args::path?
  grpc_slice service_method;

  // The backend host that the client thinks it's talking to. This may be
  // different from the actual backend in the case of, for example,
  // load-balanced targets. We store a copy of the metadata slice in order to
  // lowercase it. */
  char* target_host;
  size_t target_host_len;

  // The client IP address (including a length prefix) and the load reporting
  // token.
  char* client_ip_and_lr_token;
  size_t client_ip_and_lr_token_len;

  // Whether a call was recorded when it started. We want to record the end of
  // a call iff there's a corresponding start. Note that it's possible that we
  // attempt to record the call end before we have recorded the call start,
  // because the data needed for recording the start comes from the initial
  // metadata, which may not be ready before the call finishes.
  bool was_start_recorded;
} call_data;

typedef struct channel_data {
  // The peer's authenticated identity, if available. NULL otherwise.
  char* peer_identity;
  size_t peer_identity_len;
} channel_data;

// From the peer_string in calld, extracts the client IP string (owned by
// caller), e.g., "01020a0b". Upon failure, set the output pointer to null and
// size to zero.
void get_census_safe_client_ip_string(const call_data* calld,
                                      char** client_ip_string, size_t* size) {
  // Find the client URI string.
  auto client_uri_str =
      reinterpret_cast<const char*>(gpr_atm_acq_load(calld->peer_string));
  if (client_uri_str == nullptr) {
    gpr_log(GPR_ERROR,
            "Unable to extract client URI string (peer string) from gRPC "
            "metadata.");
    *client_ip_string = nullptr;
    *size = 0;
    return;
  }
  // Parse the client URI string into grpc_uri.
  grpc_uri* client_uri = grpc_uri_parse(client_uri_str, true);
  if (client_uri == nullptr) {
    gpr_log(GPR_ERROR,
            "Unable to parse the client URI string (peer string) to a client "
            "URI.");
    *client_ip_string = nullptr;
    *size = 0;
    return;
  }
  // Parse the client URI into grpc_resolved_address.
  grpc_resolved_address resolved_address;
  bool success = grpc_parse_uri(client_uri, &resolved_address);
  grpc_uri_destroy(client_uri);
  if (!success) {
    gpr_log(GPR_ERROR,
            "Unable to parse client URI into a grpc_resolved_address.");
    *client_ip_string = nullptr;
    *size = 0;
    return;
  }
  // Convert the socket address in the grpc_resolved_address into a hex string
  // according to the address family.
  grpc_sockaddr* addr = reinterpret_cast<grpc_sockaddr*>(resolved_address.addr);
  if (addr->sa_family == GRPC_AF_INET) {
    grpc_sockaddr_in* addr4 = reinterpret_cast<grpc_sockaddr_in*>(addr);
    gpr_asprintf(client_ip_string, "%08x", addr4->sin_addr);
    *size = 8;
  } else if (addr->sa_family == GRPC_AF_INET6) {
    grpc_sockaddr_in6* addr6 = reinterpret_cast<grpc_sockaddr_in6*>(addr);
    gpr_asprintf(client_ip_string, "%032x", addr6->sin6_addr);
    *size = 32;
  } else {
    GPR_UNREACHABLE_CODE();
  }
}

// Concatenates the client IP address and the load reporting token, then
// stores the result into the call data.
void store_client_ip_and_lr_token(const char* lr_token, size_t lr_token_len,
                                  call_data* calld) {
  char* client_ip;
  size_t client_ip_len;
  get_census_safe_client_ip_string(calld, &client_ip, &client_ip_len);
  calld->client_ip_and_lr_token_len =
      kLengthPrefixSize + client_ip_len + lr_token_len;
  calld->client_ip_and_lr_token = static_cast<char*>(
      gpr_zalloc(calld->client_ip_and_lr_token_len * sizeof(char)));
  char* cur_pos = calld->client_ip_and_lr_token;
  // Store the IP length prefix.
  if (client_ip_len == 0) {
    strncpy(cur_pos, kEmptyAddressLengthString, kLengthPrefixSize);
  } else if (client_ip_len == 8) {
    strncpy(cur_pos, kEncodedIpv4AddressLengthString, kLengthPrefixSize);
  } else if (client_ip_len == 32) {
    strncpy(cur_pos, kEncodedIpv6AddressLengthString, kLengthPrefixSize);
  } else {
    GPR_UNREACHABLE_CODE();
  }
  cur_pos += kLengthPrefixSize;
  // Store the IP.
  if (client_ip_len != 0) {
    strncpy(cur_pos, client_ip, client_ip_len);
  }
  gpr_free(client_ip);
  cur_pos += client_ip_len;
  // Store the LR token.
  if (lr_token_len != 0) {
    strncpy(cur_pos, lr_token, lr_token_len);
  }
  GPR_ASSERT(cur_pos + lr_token_len - calld->client_ip_and_lr_token ==
             calld->client_ip_and_lr_token_len);
}

// From the initial metadata, extracts the service_method, target_host, and
// ip_and_lr_token.
grpc_filtered_mdelem recv_initial_md_filter(void* user_data, grpc_mdelem md) {
  grpc_call_element* elem = reinterpret_cast<grpc_call_element*>(user_data);
  call_data* calld = reinterpret_cast<call_data*>(elem->call_data);
  if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_PATH)) {
    calld->service_method = grpc_slice_ref_internal(GRPC_MDVALUE(md));
  } else if (calld->target_host == nullptr &&
             grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_AUTHORITY)) {
    // TODO(juanlishen): The internal comment says the target host is constant
    // per channel and we only bother processing the value once. Is that
    // constant? The current code is actually processing this for every call?
    grpc_slice target_host_slice = GRPC_MDVALUE(md);
    calld->target_host_len = GRPC_SLICE_LENGTH(target_host_slice);
    calld->target_host =
        reinterpret_cast<char*>(gpr_zalloc(calld->target_host_len));
    for (size_t i = 0; i < calld->target_host_len; ++i) {
      calld->target_host[i] = static_cast<char>(
          tolower(GRPC_SLICE_START_PTR(target_host_slice)[i]));
    }
  } else if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_LB_TOKEN)) {
    if (calld->client_ip_and_lr_token == nullptr) {
      store_client_ip_and_lr_token(
          reinterpret_cast<const char*> GRPC_SLICE_START_PTR(GRPC_MDVALUE(md)),
          GRPC_SLICE_LENGTH(GRPC_MDVALUE(md)), calld);
    }
    return GRPC_FILTERED_REMOVE();
  }
  return GRPC_FILTERED_MDELEM(md);
}

// Records the call start.
void wrapped_recv_initial_metadata_ready(void* arg, grpc_error* err) {
  grpc_call_element* elem = reinterpret_cast<grpc_call_element*>(arg);
  call_data* calld = reinterpret_cast<call_data*>(elem->call_data);
  channel_data* chand = reinterpret_cast<channel_data*>(elem->channel_data);
  if (err == GRPC_ERROR_NONE) {
    GRPC_LOG_IF_ERROR("server_load_reporting_filter",
                      grpc_metadata_batch_filter(
                          calld->recv_initial_metadata, recv_initial_md_filter,
                          elem, "recv_initial_metadata filtering error"));
    // If the LB token was not found in the recv_initial_metadata, only the
    // client IP part will be recorded (with an empty LB token).
    if (calld->client_ip_and_lr_token == nullptr) {
      store_client_ip_and_lr_token(nullptr, 0, calld);
    }
    opencensus::stats::Record(
        {{::grpc::load_reporter::MeasureStartCount(), 1}},
        {{::grpc::load_reporter::TagKeyToken(),
          {calld->client_ip_and_lr_token, calld->client_ip_and_lr_token_len}},
         {::grpc::load_reporter::TagKeyHost(),
          {calld->target_host, calld->target_host_len}},
         {::grpc::load_reporter::TagKeyUserId(),
          {chand->peer_identity, chand->peer_identity_len}}});
    calld->was_start_recorded = true;
  }
  // TODO(juanlishen): Ask yashkt; use RUN instead.
  GRPC_CLOSURE_SCHED(calld->original_recv_initial_metadata_ready,
                     GRPC_ERROR_REF(err));
}

typedef struct cost_entry {
  double cost;
  const char cost_name_start[];
} cost_entry;

// Ctor for call_data.
grpc_error* init_call_elem(grpc_call_element* elem,
                           const grpc_call_element_args* args) {
  call_data* calld = reinterpret_cast<call_data*>(elem->call_data);
  memset(calld, 0, sizeof(call_data));
  calld->service_method = grpc_empty_slice();
  GRPC_CLOSURE_INIT(&calld->wrapped_recv_initial_metadata_ready,
                    wrapped_recv_initial_metadata_ready, elem,
                    grpc_schedule_on_exec_ctx);
  return GRPC_ERROR_NONE;
}

// Records the other call metrics.
grpc_filtered_mdelem send_trailing_md_filter(void* user_data, grpc_mdelem md) {
  grpc_call_element* elem = reinterpret_cast<grpc_call_element*>(user_data);
  call_data* calld = reinterpret_cast<call_data*>(elem->call_data);
  channel_data* chand = reinterpret_cast<channel_data*>(elem->channel_data);
  // TODO(juanlishen): GRPC_MDSTR_LB_COST_BIN meaning?
  if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_LB_COST_BIN)) {
    const grpc_slice value = GRPC_MDVALUE(md);
    const size_t cost_entry_size = GRPC_SLICE_LENGTH(value);
    if (cost_entry_size < sizeof(double)) {
      gpr_log(GPR_ERROR,
              "Cost metadata value too small (%zu bytes) to hold valid data. "
              "Ignoring.",
              cost_entry_size);
      return GRPC_FILTERED_REMOVE();
    }
    const cost_entry* cost_entry_data =
        reinterpret_cast<const cost_entry*>(GRPC_SLICE_START_PTR(value));
    const char* cost_name = cost_entry_data->cost_name_start;
    const size_t cost_name_len = cost_entry_size - sizeof(double);
    opencensus::stats::Record(
        {{::grpc::load_reporter::MeasureOtherCallMetric(),
          cost_entry_data->cost}},
        {{::grpc::load_reporter::TagKeyToken(),
          {calld->client_ip_and_lr_token, calld->client_ip_and_lr_token_len}},
         {::grpc::load_reporter::TagKeyHost(),
          {calld->target_host, calld->target_host_len}},
         {::grpc::load_reporter::TagKeyUserId(),
          {chand->peer_identity, chand->peer_identity_len}},
         {::grpc::load_reporter::TagKeyMetricName(),
          {cost_name, cost_name_len}}});
    return GRPC_FILTERED_REMOVE();
  }
  return GRPC_FILTERED_MDELEM(md);
}

// This matches the classification of the status codes in
// googleapis/google/rpc/code.proto.
const char* GetStatusTagForStatus(grpc_status_code status) {
  switch (status) {
    case GRPC_STATUS_OK:
      return ::grpc::load_reporter::kCallStatusOk;
    case GRPC_STATUS_UNKNOWN:
    case GRPC_STATUS_DEADLINE_EXCEEDED:
    case GRPC_STATUS_UNIMPLEMENTED:
    case GRPC_STATUS_INTERNAL:
    case GRPC_STATUS_UNAVAILABLE:
    case GRPC_STATUS_DATA_LOSS:
      return ::grpc::load_reporter::kCallStatusServerError;
    default:
      return ::grpc::load_reporter::kCallStatusClientError;
  }
}

// Dtor for call_data.
// Records the call end.
void destroy_call_elem(grpc_call_element* elem,
                       const grpc_call_final_info* final_info,
                       grpc_closure* ignored) {
  channel_data* chand = reinterpret_cast<channel_data*>(elem->channel_data);
  call_data* calld = reinterpret_cast<call_data*>(elem->call_data);
  // Only record an end if we've recorded its corresponding start.
  if (calld->was_start_recorded) {
    opencensus::stats::Record(
        {{::grpc::load_reporter::MeasureEndCount(), 1},
         {::grpc::load_reporter::MeasureEndBytesSent(),
          final_info->stats.transport_stream_stats.outgoing.data_bytes},
         {::grpc::load_reporter::MeasureEndBytesReceived(),
          final_info->stats.transport_stream_stats.incoming.data_bytes},
         {::grpc::load_reporter::MeasureEndLatencyMs(),
          gpr_time_to_millis(final_info->stats.latency)}},
        {{::grpc::load_reporter::TagKeyToken(),
          {calld->client_ip_and_lr_token, calld->client_ip_and_lr_token_len}},
         {::grpc::load_reporter::TagKeyHost(),
          {calld->target_host, calld->target_host_len}},
         {::grpc::load_reporter::TagKeyUserId(),
          {chand->peer_identity, chand->peer_identity_len}},
         {::grpc::load_reporter::TagKeyStatus(),
          GetStatusTagForStatus(final_info->final_status)}});
  }
  grpc_slice_unref_internal(calld->service_method);
}

// Ctor for channel_data.
grpc_error* init_channel_elem(grpc_channel_element* elem,
                              grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
  channel_data* chand = reinterpret_cast<channel_data*>(elem->channel_data);
  memset(chand, 0, sizeof(channel_data));
  // Find and record the peer_identity.
  const grpc_auth_context* auth_context =
      grpc_find_auth_context_in_args(args->channel_args);
  if (auth_context != nullptr &&
      grpc_auth_context_peer_is_authenticated(auth_context)) {
    grpc_auth_property_iterator auth_it =
        grpc_auth_context_peer_identity(auth_context);
    const grpc_auth_property* auth_property =
        grpc_auth_property_iterator_next(&auth_it);
    if (auth_property != nullptr) {
      chand->peer_identity = auth_property->value;
      chand->peer_identity_len = auth_property->value_length;
    }
  }
  return GRPC_ERROR_NONE;
}

// Dtor for channel data.
void destroy_channel_elem(grpc_channel_element* elem) {}

void start_transport_stream_op_batch(grpc_call_element* elem,
                                     grpc_transport_stream_op_batch* op) {
  GPR_TIMER_SCOPE("lr_start_transport_stream_op", 0);
  call_data* calld = reinterpret_cast<call_data*>(elem->call_data);
  if (op->recv_initial_metadata) {
    // Save some fields to use when initial metadata is ready.
    calld->peer_string = op->payload->recv_initial_metadata.peer_string;
    calld->recv_initial_metadata =
        op->payload->recv_initial_metadata.recv_initial_metadata;
    calld->original_recv_initial_metadata_ready =
        op->payload->recv_initial_metadata.recv_initial_metadata_ready;
    // Substitute the original closure for the wrapped closure.
    op->payload->recv_initial_metadata.recv_initial_metadata_ready =
        &calld->wrapped_recv_initial_metadata_ready;
  } else if (op->send_trailing_metadata) {
    GRPC_LOG_IF_ERROR(
        "server_load_reporting_filter",
        grpc_metadata_batch_filter(
            op->payload->send_trailing_metadata.send_trailing_metadata,
            send_trailing_md_filter, elem,
            "send_trailing_metadata filtering error"));
  }
  grpc_call_next_op(elem, op);
}

}  // namespace

const grpc_channel_filter grpc_server_load_reporting_filter = {
    start_transport_stream_op_batch,
    grpc_channel_next_op,
    sizeof(call_data),
    init_call_elem,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    destroy_call_elem,
    sizeof(channel_data),
    init_channel_elem,
    destroy_channel_elem,
    grpc_channel_next_get_info,
    "server_load_reporting"};
