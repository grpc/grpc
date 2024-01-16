//
//
// Copyright 2016 gRPC authors.
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

#include "src/core/ext/filters/load_reporting/server_load_reporting_filter.h"

#include <stdint.h>
#include <stdlib.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "opencensus/stats/stats.h"
#include "opencensus/tags/tag_key.h"

#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/status.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/ext/filters/load_reporting/registered_opencensus_objects.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/channel/call_finalization.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/cpp/server/load_reporter/constants.h"

// IWYU pragma: no_include "opencensus/stats/recording.h"

namespace grpc_core {

constexpr char kEncodedIpv4AddressLengthString[] = "08";
constexpr char kEncodedIpv6AddressLengthString[] = "32";
constexpr char kEmptyAddressLengthString[] = "00";

const NoInterceptor ServerLoadReportingFilter::Call::OnServerInitialMetadata;
const NoInterceptor ServerLoadReportingFilter::Call::OnClientToServerMessage;
const NoInterceptor ServerLoadReportingFilter::Call::OnServerToClientMessage;

absl::StatusOr<ServerLoadReportingFilter> ServerLoadReportingFilter::Create(
    const ChannelArgs& channel_args, ChannelFilter::Args) {
  // Find and record the peer_identity.
  ServerLoadReportingFilter filter;
  const auto* auth_context = channel_args.GetObject<grpc_auth_context>();
  if (auth_context != nullptr &&
      grpc_auth_context_peer_is_authenticated(auth_context)) {
    grpc_auth_property_iterator auth_it =
        grpc_auth_context_peer_identity(auth_context);
    const grpc_auth_property* auth_property =
        grpc_auth_property_iterator_next(&auth_it);
    if (auth_property != nullptr) {
      filter.peer_identity_ =
          std::string(auth_property->value, auth_property->value_length);
    }
  }
  return std::move(filter);
}

namespace {
std::string GetCensusSafeClientIpString(
    const ClientMetadata& initial_metadata) {
  // Find the client URI string.
  const Slice* client_uri_slice = initial_metadata.get_pointer(PeerString());
  if (client_uri_slice == nullptr) {
    gpr_log(GPR_ERROR,
            "Unable to extract client URI string (peer string) from gRPC "
            "metadata.");
    return "";
  }
  absl::StatusOr<URI> client_uri =
      URI::Parse(client_uri_slice->as_string_view());
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
    GPR_UNREACHABLE_CODE(abort());
  }
}

std::string MakeClientIpAndLrToken(absl::string_view lr_token,
                                   const ClientMetadata& initial_metadata) {
  std::string client_ip = GetCensusSafeClientIpString(initial_metadata);
  absl::string_view prefix;
  switch (client_ip.length()) {
    case 0:
      prefix = kEmptyAddressLengthString;
      break;
    case 8:
      prefix = kEncodedIpv4AddressLengthString;
      break;
    case 32:
      prefix = kEncodedIpv6AddressLengthString;
      break;
    default:
      GPR_UNREACHABLE_CODE(abort());
  }
  return absl::StrCat(prefix, client_ip, lr_token);
}

const char* GetStatusTagForStatus(grpc_status_code status) {
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
}  // namespace

void ServerLoadReportingFilter::Call::OnClientInitialMetadata(
    ClientMetadata& md, ServerLoadReportingFilter* filter) {
  // Gather up basic facts about the request
  Slice service_method;
  if (const Slice* path = md.get_pointer(HttpPathMetadata())) {
    service_method = path->Ref();
  }
  if (const Slice* authority = md.get_pointer(HttpAuthorityMetadata())) {
    target_host_ = absl::AsciiStrToLower(authority->as_string_view());
  }
  auto lb_token = md.Take(LbTokenMetadata()).value_or(Slice());
  client_ip_and_lr_token_ =
      MakeClientIpAndLrToken(lb_token.as_string_view(), md);
  // Record the beginning of the request
  opencensus::stats::Record(
      {{::grpc::load_reporter::MeasureStartCount(), 1}},
      {{::grpc::load_reporter::TagKeyToken(),
        {client_ip_and_lr_token_.data(), client_ip_and_lr_token_.length()}},
       {::grpc::load_reporter::TagKeyHost(),
        {target_host_.data(), target_host_.length()}},
       {::grpc::load_reporter::TagKeyUserId(),
        {filter->peer_identity_.data(), filter->peer_identity_.length()}}});
}

void ServerLoadReportingFilter::Call::OnServerTrailingMetadata(
    ServerMetadata& md, ServerLoadReportingFilter* filter) {
  const auto& costs = md.Take(LbCostBinMetadata());
  for (const auto& cost : costs) {
    opencensus::stats::Record(
        {{::grpc::load_reporter::MeasureOtherCallMetric(), cost.cost}},
        {{::grpc::load_reporter::TagKeyToken(),
          {client_ip_and_lr_token_.data(), client_ip_and_lr_token_.length()}},
         {::grpc::load_reporter::TagKeyHost(),
          {target_host_.data(), target_host_.length()}},
         {::grpc::load_reporter::TagKeyUserId(),
          {filter->peer_identity_.data(), filter->peer_identity_.length()}},
         {::grpc::load_reporter::TagKeyMetricName(),
          {cost.name.data(), cost.name.length()}}});
  }
}

void ServerLoadReportingFilter::Call::OnFinalize(
    const grpc_call_final_info* final_info, ServerLoadReportingFilter* filter) {
  if (final_info == nullptr) return;
  // After the last bytes have been placed on the wire we record
  // final measurements
  opencensus::stats::Record(
      {{::grpc::load_reporter::MeasureEndCount(), 1},
       {::grpc::load_reporter::MeasureEndBytesSent(),
        final_info->stats.transport_stream_stats.outgoing.data_bytes},
       {::grpc::load_reporter::MeasureEndBytesReceived(),
        final_info->stats.transport_stream_stats.incoming.data_bytes},
       {::grpc::load_reporter::MeasureEndLatencyMs(),
        gpr_time_to_millis(final_info->stats.latency)}},
      {{::grpc::load_reporter::TagKeyToken(),
        {client_ip_and_lr_token_.data(), client_ip_and_lr_token_.length()}},
       {::grpc::load_reporter::TagKeyHost(),
        {target_host_.data(), target_host_.length()}},
       {::grpc::load_reporter::TagKeyUserId(),
        {filter->peer_identity_.data(), filter->peer_identity_.length()}},
       {::grpc::load_reporter::TagKeyStatus(),
        GetStatusTagForStatus(final_info->final_status)}});
}

const grpc_channel_filter ServerLoadReportingFilter::kFilter =
    MakePromiseBasedFilter<ServerLoadReportingFilter, FilterEndpoint::kServer>(
        "server_load_reporting");

// TODO(juanlishen): We should register the filter during grpc initialization
// time once OpenCensus is compatible with our build system. For now, we force
// registration of the server load reporting filter at static initialization
// time if we build with the filter target.
struct ServerLoadReportingFilterStaticRegistrar {
  ServerLoadReportingFilterStaticRegistrar() {
    CoreConfiguration::RegisterBuilder([](CoreConfiguration::Builder* builder) {
      // Access measures to ensure they are initialized. Otherwise, we can't
      // create any valid view before the first RPC.
      grpc::load_reporter::MeasureStartCount();
      grpc::load_reporter::MeasureEndCount();
      grpc::load_reporter::MeasureEndBytesSent();
      grpc::load_reporter::MeasureEndBytesReceived();
      grpc::load_reporter::MeasureEndLatencyMs();
      grpc::load_reporter::MeasureOtherCallMetric();
      builder->channel_init()
          ->RegisterFilter<ServerLoadReportingFilter>(GRPC_SERVER_CHANNEL)
          .IfChannelArg(GRPC_ARG_ENABLE_LOAD_REPORTING, false);
    });
  }
} server_load_reporting_filter_static_registrar;

}  // namespace grpc_core
