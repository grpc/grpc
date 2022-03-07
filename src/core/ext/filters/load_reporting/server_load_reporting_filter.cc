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

#include "src/core/ext/filters/load_reporting/server_load_reporting_filter.h"

#include <string.h>

#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h"
#include "src/core/ext/filters/load_reporting/registered_opencensus_objects.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/context.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/call.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc {

constexpr char kEncodedIpv4AddressLengthString[] = "08";
constexpr char kEncodedIpv6AddressLengthString[] = "32";
constexpr char kEmptyAddressLengthString[] = "00";
constexpr size_t kLengthPrefixSize = 2;

grpc_error_handle ServerLoadReportingChannelData::Init(
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
    grpc_closure* /*then_call_closure*/) {
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
          {target_host_.data(), target_host_.length()}},
         {::grpc::load_reporter::TagKeyUserId(),
          {chand->peer_identity(), chand->peer_identity_len()}},
         {::grpc::load_reporter::TagKeyStatus(),
          GetStatusTagForStatus(final_info->final_status)}});
    gpr_free(client_ip_and_lr_token_);
  }
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
  }
  if (op->send_trailing_metadata() != nullptr) {
    const auto& costs = op->send_trailing_metadata()->batch()->Take(
        grpc_core::LbCostBinMetadata());
    for (const auto& cost : costs) {
      ServerLoadReportingChannelData* chand =
          reinterpret_cast<ServerLoadReportingChannelData*>(elem->channel_data);
      opencensus::stats::Record(
          {{::grpc::load_reporter::MeasureOtherCallMetric(), cost.cost}},
          {{::grpc::load_reporter::TagKeyToken(),
            {client_ip_and_lr_token_, client_ip_and_lr_token_len_}},
           {::grpc::load_reporter::TagKeyHost(),
            {target_host_.data(), target_host_.length()}},
           {::grpc::load_reporter::TagKeyUserId(),
            {chand->peer_identity(), chand->peer_identity_len()}},
           {::grpc::load_reporter::TagKeyMetricName(),
            {cost.name.data(), cost.name.length()}}});
    }
  }
  grpc_call_next_op(elem, op->op());
}

std::string ServerLoadReportingCallData::GetCensusSafeClientIpString() {
  // Find the client URI string.
  const char* client_uri_str =
      reinterpret_cast<const char*>(gpr_atm_acq_load(peer_string_));
  if (client_uri_str == nullptr) {
    gpr_log(GPR_ERROR,
            "Unable to extract client URI string (peer string) from gRPC "
            "metadata.");
    return "";
  }
  absl::StatusOr<grpc_core::URI> client_uri =
      grpc_core::URI::Parse(client_uri_str);
  if (!client_uri.ok()) {
    gpr_log(GPR_ERROR,
            "Unable to parse the client URI string (peer string) to a client "
            "URI. Error: %s",
            client_uri.status().ToString().c_str());
    return "";
  }
  // Parse the client URI into grpc_resolved_address.
  grpc_resolved_address resolved_address;
  bool success = grpc_parse_uri(*client_uri, &resolved_address);
  if (!success) {
    gpr_log(GPR_ERROR,
            "Unable to parse client URI into a grpc_resolved_address.");
    return "";
  }
  // Convert the socket address in the grpc_resolved_address into a hex string
  // according to the address family.
  grpc_sockaddr* addr = reinterpret_cast<grpc_sockaddr*>(resolved_address.addr);
  if (addr->sa_family == GRPC_AF_INET) {
    grpc_sockaddr_in* addr4 = reinterpret_cast<grpc_sockaddr_in*>(addr);
    return absl::StrFormat("%08x", grpc_ntohl(addr4->sin_addr.s_addr));
  } else if (addr->sa_family == GRPC_AF_INET6) {
    grpc_sockaddr_in6* addr6 = reinterpret_cast<grpc_sockaddr_in6*>(addr);
    std::string client_ip;
    client_ip.reserve(32);
    uint32_t* addr6_next_long = reinterpret_cast<uint32_t*>(&addr6->sin6_addr);
    for (size_t i = 0; i < 4; ++i) {
      absl::StrAppendFormat(&client_ip, "%08x", grpc_ntohl(*addr6_next_long++));
    }
    return client_ip;
  } else {
    GPR_UNREACHABLE_CODE();
  }
}

void ServerLoadReportingCallData::StoreClientIpAndLrToken(const char* lr_token,
                                                          size_t lr_token_len) {
  std::string client_ip = GetCensusSafeClientIpString();
  client_ip_and_lr_token_len_ =
      kLengthPrefixSize + client_ip.size() + lr_token_len;
  client_ip_and_lr_token_ = static_cast<char*>(
      gpr_zalloc(client_ip_and_lr_token_len_ * sizeof(char)));
  char* cur_pos = client_ip_and_lr_token_;
  // Store the IP length prefix.
  if (client_ip.empty()) {
    strncpy(cur_pos, kEmptyAddressLengthString, kLengthPrefixSize);
  } else if (client_ip.size() == 8) {
    strncpy(cur_pos, kEncodedIpv4AddressLengthString, kLengthPrefixSize);
  } else if (client_ip.size() == 32) {
    strncpy(cur_pos, kEncodedIpv6AddressLengthString, kLengthPrefixSize);
  } else {
    GPR_UNREACHABLE_CODE();
  }
  cur_pos += kLengthPrefixSize;
  // Store the IP.
  if (!client_ip.empty()) {
    strncpy(cur_pos, client_ip.c_str(), client_ip.size());
  }
  cur_pos += client_ip.size();
  // Store the LR token.
  if (lr_token_len != 0) {
    strncpy(cur_pos, lr_token, lr_token_len);
  }
  GPR_ASSERT(
      static_cast<size_t>(cur_pos + lr_token_len - client_ip_and_lr_token_) ==
      client_ip_and_lr_token_len_);
}

void ServerLoadReportingCallData::RecvInitialMetadataReady(
    void* arg, grpc_error_handle err) {
  grpc_call_element* elem = reinterpret_cast<grpc_call_element*>(arg);
  ServerLoadReportingCallData* calld =
      reinterpret_cast<ServerLoadReportingCallData*>(elem->call_data);
  ServerLoadReportingChannelData* chand =
      reinterpret_cast<ServerLoadReportingChannelData*>(elem->channel_data);
  if (err == GRPC_ERROR_NONE) {
    if (const grpc_core::Slice* path =
            calld->recv_initial_metadata_->get_pointer(
                grpc_core::HttpPathMetadata())) {
      calld->service_method_ = path->Ref().TakeCSlice();
    }
    if (const grpc_core::Slice* authority =
            calld->recv_initial_metadata_->get_pointer(
                grpc_core::HttpAuthorityMetadata())) {
      calld->target_host_ = absl::AsciiStrToLower(authority->as_string_view());
    }
    std::string buffer;
    auto lb_token =
        calld->recv_initial_metadata_->Take(grpc_core::LbTokenMetadata());
    if (lb_token.has_value()) {
      if (calld->client_ip_and_lr_token_ == nullptr) {
        calld->StoreClientIpAndLrToken(
            reinterpret_cast<const char*>(lb_token->data()), lb_token->size());
      }
    }
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
          {calld->target_host_.data(), calld->target_host_.length()}},
         {::grpc::load_reporter::TagKeyUserId(),
          {chand->peer_identity(), chand->peer_identity_len()}}});
  }
  grpc_core::Closure::Run(DEBUG_LOCATION,
                          calld->original_recv_initial_metadata_ready_,
                          GRPC_ERROR_REF(err));
}

grpc_error_handle ServerLoadReportingCallData::Init(
    grpc_call_element* elem, const grpc_call_element_args* /*args*/) {
  service_method_ = grpc_empty_slice();
  GRPC_CLOSURE_INIT(&recv_initial_metadata_ready_, RecvInitialMetadataReady,
                    elem, grpc_schedule_on_exec_ctx);
  return GRPC_ERROR_NONE;
}

const char* ServerLoadReportingCallData::GetStatusTagForStatus(
    grpc_status_code status) {
  switch (status) {
    case GRPC_STATUS_OK:
      return grpc::load_reporter::kCallStatusOk;
    case GRPC_STATUS_UNKNOWN:
    case GRPC_STATUS_DEADLINE_EXCEEDED:
    case GRPC_STATUS_UNIMPLEMENTED:
    case GRPC_STATUS_INTERNAL:
    case GRPC_STATUS_UNAVAILABLE:
    case GRPC_STATUS_DATA_LOSS:
      return grpc::load_reporter::kCallStatusServerError;
    default:
      return grpc::load_reporter::kCallStatusClientError;
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
    static std::atomic<bool> registered{false};
    if (registered.load(std::memory_order_acquire)) return;
    RegisterChannelFilter<ServerLoadReportingChannelData,
                          ServerLoadReportingCallData>(
        "server_load_reporting", GRPC_SERVER_CHANNEL, INT_MAX,
        MaybeAddServerLoadReportingFilter);
    // Access measures to ensure they are initialized. Otherwise, we can't
    // create any valid view before the first RPC.
    grpc::load_reporter::MeasureStartCount();
    grpc::load_reporter::MeasureEndCount();
    grpc::load_reporter::MeasureEndBytesSent();
    grpc::load_reporter::MeasureEndBytesReceived();
    grpc::load_reporter::MeasureEndLatencyMs();
    grpc::load_reporter::MeasureOtherCallMetric();
    registered.store(true, std::memory_order_release);
  }
} server_load_reporting_filter_static_registrar;

}  // namespace grpc
