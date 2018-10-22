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

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/uri_parser.h"
#include "src/core/ext/filters/load_reporting/registered_opencensus_objects.h"
#include "src/core/ext/filters/load_reporting/server_load_reporting_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr_posix.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"

namespace grpc {

constexpr char kEncodedIpv4AddressLengthString[] = "08";
constexpr char kEncodedIpv6AddressLengthString[] = "32";
constexpr char kEmptyAddressLengthString[] = "00";
constexpr size_t kLengthPrefixSize = 2;

grpc_error* ServerLoadReportingChannelData::Init(
    grpc_channel_element* /* elem */, grpc_channel_element_args* args) {
  GPR_ASSERT(!args->is_last);
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
      peer_identity_ = auth_property->value;
      peer_identity_len_ = auth_property->value_length;
    }
  }
  return GRPC_ERROR_NONE;
}

void ServerLoadReportingCallData::Destroy(
    grpc_call_element* elem, const grpc_call_final_info* final_info,
    grpc_closure* then_call_closure) {
  ServerLoadReportingChannelData* chand =
      reinterpret_cast<ServerLoadReportingChannelData*>(elem->channel_data);
  // Only record an end if we've recorded its corresponding start, which is
  // indicated by a non-null client_ip_and_lr_token_. Note that it's possible
  // that we attempt to record the call end before we have recorded the call
  // start, because the data needed for recording the start comes from the
  // initial metadata, which may not be ready before the call finishes.
  if (client_ip_and_lr_token_ != nullptr) {
    opencensus::stats::Record(
        {{::grpc::load_reporter::MeasureEndCount(), 1},
         {::grpc::load_reporter::MeasureEndBytesSent(),
          final_info->stats.transport_stream_stats.outgoing.data_bytes},
         {::grpc::load_reporter::MeasureEndBytesReceived(),
          final_info->stats.transport_stream_stats.incoming.data_bytes},
         {::grpc::load_reporter::MeasureEndLatencyMs(),
          gpr_time_to_millis(final_info->stats.latency)}},
        {{::grpc::load_reporter::TagKeyToken(),
          {client_ip_and_lr_token_, client_ip_and_lr_token_len_}},
         {::grpc::load_reporter::TagKeyHost(),
          {target_host_, target_host_len_}},
         {::grpc::load_reporter::TagKeyUserId(),
          {chand->peer_identity(), chand->peer_identity_len()}},
         {::grpc::load_reporter::TagKeyStatus(),
          GetStatusTagForStatus(final_info->final_status)}});
    gpr_free(client_ip_and_lr_token_);
  }
  gpr_free(target_host_);
  grpc_slice_unref_internal(service_method_);
}

void ServerLoadReportingCallData::StartTransportStreamOpBatch(
    grpc_call_element* elem, TransportStreamOpBatch* op) {
  GPR_TIMER_SCOPE("lr_start_transport_stream_op", 0);
  if (op->recv_initial_metadata() != nullptr) {
    // Save some fields to use when initial metadata is ready.
    peer_string_ = op->get_peer_string();
    recv_initial_metadata_ =
        op->op()->payload->recv_initial_metadata.recv_initial_metadata;
    original_recv_initial_metadata_ready_ = op->recv_initial_metadata_ready();
    // Substitute the original closure for the wrapper closure.
    op->set_recv_initial_metadata_ready(&recv_initial_metadata_ready_);
  } else if (op->send_trailing_metadata() != nullptr) {
    GRPC_LOG_IF_ERROR(
        "server_load_reporting_filter",
        grpc_metadata_batch_filter(op->send_trailing_metadata()->batch(),
                                   SendTrailingMetadataFilter, elem,
                                   "send_trailing_metadata filtering error"));
  }
  grpc_call_next_op(elem, op->op());
}

void ServerLoadReportingCallData::GetCensusSafeClientIpString(
    char** client_ip_string, size_t* size) {
  // Find the client URI string.
  const char* client_uri_str =
      reinterpret_cast<const char*>(gpr_atm_acq_load(peer_string_));
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
    gpr_asprintf(client_ip_string, "%08x", grpc_ntohl(addr4->sin_addr.s_addr));
    *size = 8;
  } else if (addr->sa_family == GRPC_AF_INET6) {
    grpc_sockaddr_in6* addr6 = reinterpret_cast<grpc_sockaddr_in6*>(addr);
    *client_ip_string = static_cast<char*>(gpr_malloc(32 + 1));
    uint32_t* addr6_next_long = reinterpret_cast<uint32_t*>(&addr6->sin6_addr);
    for (size_t i = 0; i < 4; ++i) {
      snprintf(*client_ip_string + 8 * i, 8 + 1, "%08x",
               grpc_ntohl(*addr6_next_long++));
    }
    *size = 32;
  } else {
    GPR_UNREACHABLE_CODE();
  }
}

void ServerLoadReportingCallData::StoreClientIpAndLrToken(const char* lr_token,
                                                          size_t lr_token_len) {
  char* client_ip;
  size_t client_ip_len;
  GetCensusSafeClientIpString(&client_ip, &client_ip_len);
  client_ip_and_lr_token_len_ =
      kLengthPrefixSize + client_ip_len + lr_token_len;
  client_ip_and_lr_token_ = static_cast<char*>(
      gpr_zalloc(client_ip_and_lr_token_len_ * sizeof(char)));
  char* cur_pos = client_ip_and_lr_token_;
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
  GPR_ASSERT(cur_pos + lr_token_len - client_ip_and_lr_token_ ==
             client_ip_and_lr_token_len_);
}

grpc_filtered_mdelem ServerLoadReportingCallData::RecvInitialMetadataFilter(
    void* user_data, grpc_mdelem md) {
  grpc_call_element* elem = reinterpret_cast<grpc_call_element*>(user_data);
  ServerLoadReportingCallData* calld =
      reinterpret_cast<ServerLoadReportingCallData*>(elem->call_data);
  if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_PATH)) {
    calld->service_method_ = grpc_slice_ref_internal(GRPC_MDVALUE(md));
  } else if (calld->target_host_ == nullptr &&
             grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_AUTHORITY)) {
    grpc_slice target_host_slice = GRPC_MDVALUE(md);
    calld->target_host_len_ = GRPC_SLICE_LENGTH(target_host_slice);
    calld->target_host_ =
        reinterpret_cast<char*>(gpr_zalloc(calld->target_host_len_));
    for (size_t i = 0; i < calld->target_host_len_; ++i) {
      calld->target_host_[i] = static_cast<char>(
          tolower(GRPC_SLICE_START_PTR(target_host_slice)[i]));
    }
  } else if (grpc_slice_eq(GRPC_MDKEY(md), GRPC_MDSTR_LB_TOKEN)) {
    if (calld->client_ip_and_lr_token_ == nullptr) {
      calld->StoreClientIpAndLrToken(
          reinterpret_cast<const char*> GRPC_SLICE_START_PTR(GRPC_MDVALUE(md)),
          GRPC_SLICE_LENGTH(GRPC_MDVALUE(md)));
    }
    return GRPC_FILTERED_REMOVE();
  }
  return GRPC_FILTERED_MDELEM(md);
}

void ServerLoadReportingCallData::RecvInitialMetadataReady(void* arg,
                                                           grpc_error* err) {
  grpc_call_element* elem = reinterpret_cast<grpc_call_element*>(arg);
  ServerLoadReportingCallData* calld =
      reinterpret_cast<ServerLoadReportingCallData*>(elem->call_data);
  ServerLoadReportingChannelData* chand =
      reinterpret_cast<ServerLoadReportingChannelData*>(elem->channel_data);
  if (err == GRPC_ERROR_NONE) {
    GRPC_LOG_IF_ERROR(
        "server_load_reporting_filter",
        grpc_metadata_batch_filter(calld->recv_initial_metadata_,
                                   RecvInitialMetadataFilter, elem,
                                   "recv_initial_metadata filtering error"));
    // If the LB token was not found in the recv_initial_metadata, only the
    // client IP part will be recorded (with an empty LB token).
    if (calld->client_ip_and_lr_token_ == nullptr) {
      calld->StoreClientIpAndLrToken(nullptr, 0);
    }
    opencensus::stats::Record(
        {{::grpc::load_reporter::MeasureStartCount(), 1}},
        {{::grpc::load_reporter::TagKeyToken(),
          {calld->client_ip_and_lr_token_, calld->client_ip_and_lr_token_len_}},
         {::grpc::load_reporter::TagKeyHost(),
          {calld->target_host_, calld->target_host_len_}},
         {::grpc::load_reporter::TagKeyUserId(),
          {chand->peer_identity(), chand->peer_identity_len()}}});
  }
  GRPC_CLOSURE_RUN(calld->original_recv_initial_metadata_ready_,
                   GRPC_ERROR_REF(err));
}

grpc_error* ServerLoadReportingCallData::Init(
    grpc_call_element* elem, const grpc_call_element_args* args) {
  service_method_ = grpc_empty_slice();
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
  return GRPC_ERROR_NONE;
}

grpc_filtered_mdelem ServerLoadReportingCallData::SendTrailingMetadataFilter(
    void* user_data, grpc_mdelem md) {
  grpc_call_element* elem = reinterpret_cast<grpc_call_element*>(user_data);
  ServerLoadReportingCallData* calld =
      reinterpret_cast<ServerLoadReportingCallData*>(elem->call_data);
  ServerLoadReportingChannelData* chand =
      reinterpret_cast<ServerLoadReportingChannelData*>(elem->channel_data);
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
    const double* cost_entry_ptr =
        reinterpret_cast<const double*>(GRPC_SLICE_START_PTR(value));
    double cost_value = *cost_entry_ptr++;
    const char* cost_name = reinterpret_cast<const char*>(cost_entry_ptr);
    const size_t cost_name_len = cost_entry_size - sizeof(double);
    opencensus::stats::Record(
        {{::grpc::load_reporter::MeasureOtherCallMetric(), cost_value}},
        {{::grpc::load_reporter::TagKeyToken(),
          {calld->client_ip_and_lr_token_, calld->client_ip_and_lr_token_len_}},
         {::grpc::load_reporter::TagKeyHost(),
          {calld->target_host_, calld->target_host_len_}},
         {::grpc::load_reporter::TagKeyUserId(),
          {chand->peer_identity(), chand->peer_identity_len()}},
         {::grpc::load_reporter::TagKeyMetricName(),
          {cost_name, cost_name_len}}});
    return GRPC_FILTERED_REMOVE();
  }
  return GRPC_FILTERED_MDELEM(md);
}

const char* ServerLoadReportingCallData::GetStatusTagForStatus(
    grpc_status_code status) {
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

namespace {
bool MaybeAddServerLoadReportingFilter(const grpc_channel_args& args) {
  return grpc_channel_arg_get_bool(
      grpc_channel_args_find(&args, GRPC_ARG_ENABLE_LOAD_REPORTING), false);
}
}  // namespace

// TODO(juanlishen): We should register the filter during grpc initialization
// time once OpenCensus is compatible with our build system. For now, we force
// registration of the server load reporting filter at static initialization
// time if we build with the filter target.
struct ServerLoadReportingFilterStaticRegistrar {
  ServerLoadReportingFilterStaticRegistrar() {
    static std::atomic_bool registered{false};
    if (registered) return;
    RegisterChannelFilter<ServerLoadReportingChannelData,
                          ServerLoadReportingCallData>(
        "server_load_reporting", GRPC_SERVER_CHANNEL, INT_MAX,
        MaybeAddServerLoadReportingFilter);
    // Access measures to ensure they are initialized. Otherwise, we can't
    // create any valid view before the first RPC.
    ::grpc::load_reporter::MeasureStartCount();
    ::grpc::load_reporter::MeasureEndCount();
    ::grpc::load_reporter::MeasureEndBytesSent();
    ::grpc::load_reporter::MeasureEndBytesReceived();
    ::grpc::load_reporter::MeasureEndLatencyMs();
    ::grpc::load_reporter::MeasureOtherCallMetric();
    registered = true;
  }
} server_load_reporting_filter_static_registrar;

}  // namespace grpc
