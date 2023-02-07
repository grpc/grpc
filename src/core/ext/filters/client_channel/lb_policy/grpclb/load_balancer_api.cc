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

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h"

#include <string.h>

#include <algorithm>

#include "google/protobuf/duration.upb.h"
#include "google/protobuf/timestamp.upb.h"
#include "upb/upb.h"

#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/proto/grpc/lb/v1/load_balancer.upb.h"

namespace grpc_core {

bool GrpcLbServer::operator==(const GrpcLbServer& other) const {
  if (ip_size != other.ip_size) return false;
  int r = memcmp(ip_addr, other.ip_addr, ip_size);
  if (r != 0) return false;
  if (port != other.port) return false;
  r = strncmp(load_balance_token, other.load_balance_token,
              sizeof(load_balance_token));
  if (r != 0) return false;
  return drop == other.drop;
}

namespace {

grpc_slice grpc_grpclb_request_encode(
    const grpc_lb_v1_LoadBalanceRequest* request, upb_Arena* arena) {
  size_t buf_length;
  char* buf =
      grpc_lb_v1_LoadBalanceRequest_serialize(request, arena, &buf_length);
  return grpc_slice_from_copied_buffer(buf, buf_length);
}

}  // namespace

grpc_slice GrpcLbRequestCreate(const char* lb_service_name, upb_Arena* arena) {
  grpc_lb_v1_LoadBalanceRequest* req = grpc_lb_v1_LoadBalanceRequest_new(arena);
  grpc_lb_v1_InitialLoadBalanceRequest* initial_request =
      grpc_lb_v1_LoadBalanceRequest_mutable_initial_request(req, arena);
  size_t name_len = std::min(strlen(lb_service_name),
                             size_t{GRPC_GRPCLB_SERVICE_NAME_MAX_LENGTH});
  grpc_lb_v1_InitialLoadBalanceRequest_set_name(
      initial_request,
      upb_StringView_FromDataAndSize(lb_service_name, name_len));
  return grpc_grpclb_request_encode(req, arena);
}

namespace {

void google_protobuf_Timestamp_assign(google_protobuf_Timestamp* timestamp,
                                      const gpr_timespec& value) {
  google_protobuf_Timestamp_set_seconds(timestamp, value.tv_sec);
  google_protobuf_Timestamp_set_nanos(timestamp, value.tv_nsec);
}

}  // namespace

grpc_slice GrpcLbLoadReportRequestCreate(
    int64_t num_calls_started, int64_t num_calls_finished,
    int64_t num_calls_finished_with_client_failed_to_send,
    int64_t num_calls_finished_known_received,
    const GrpcLbClientStats::DroppedCallCounts* drop_token_counts,
    upb_Arena* arena) {
  grpc_lb_v1_LoadBalanceRequest* req = grpc_lb_v1_LoadBalanceRequest_new(arena);
  grpc_lb_v1_ClientStats* req_stats =
      grpc_lb_v1_LoadBalanceRequest_mutable_client_stats(req, arena);
  google_protobuf_Timestamp_assign(
      grpc_lb_v1_ClientStats_mutable_timestamp(req_stats, arena),
      gpr_now(GPR_CLOCK_REALTIME));
  grpc_lb_v1_ClientStats_set_num_calls_started(req_stats, num_calls_started);
  grpc_lb_v1_ClientStats_set_num_calls_finished(req_stats, num_calls_finished);
  grpc_lb_v1_ClientStats_set_num_calls_finished_with_client_failed_to_send(
      req_stats, num_calls_finished_with_client_failed_to_send);
  grpc_lb_v1_ClientStats_set_num_calls_finished_known_received(
      req_stats, num_calls_finished_known_received);
  if (drop_token_counts != nullptr) {
    for (size_t i = 0; i < drop_token_counts->size(); ++i) {
      const GrpcLbClientStats::DropTokenCount& cur = (*drop_token_counts)[i];
      grpc_lb_v1_ClientStatsPerToken* cur_msg =
          grpc_lb_v1_ClientStats_add_calls_finished_with_drop(req_stats, arena);
      const size_t token_len = strlen(cur.token.get());
      char* token = reinterpret_cast<char*>(upb_Arena_Malloc(arena, token_len));
      memcpy(token, cur.token.get(), token_len);
      grpc_lb_v1_ClientStatsPerToken_set_load_balance_token(
          cur_msg, upb_StringView_FromDataAndSize(token, token_len));
      grpc_lb_v1_ClientStatsPerToken_set_num_calls(cur_msg, cur.count);
    }
  }
  return grpc_grpclb_request_encode(req, arena);
}

namespace {

bool ParseServerList(const grpc_lb_v1_LoadBalanceResponse& response,
                     std::vector<GrpcLbServer>* server_list) {
  // Determine the number of servers.
  const grpc_lb_v1_ServerList* server_list_msg =
      grpc_lb_v1_LoadBalanceResponse_server_list(&response);
  if (server_list_msg == nullptr) return false;
  size_t server_count = 0;
  const grpc_lb_v1_Server* const* servers =
      grpc_lb_v1_ServerList_servers(server_list_msg, &server_count);
  // Populate servers.
  if (server_count > 0) {
    server_list->reserve(server_count);
    for (size_t i = 0; i < server_count; ++i) {
      GrpcLbServer& cur = *server_list->emplace(server_list->end());
      upb_StringView address = grpc_lb_v1_Server_ip_address(servers[i]);
      if (address.size == 0) {
        ;  // Nothing to do because cur->ip_address is an empty string.
      } else if (address.size <= GRPC_GRPCLB_SERVER_IP_ADDRESS_MAX_SIZE) {
        cur.ip_size = static_cast<int32_t>(address.size);
        memcpy(cur.ip_addr, address.data, address.size);
      }
      cur.port = grpc_lb_v1_Server_port(servers[i]);
      upb_StringView token = grpc_lb_v1_Server_load_balance_token(servers[i]);
      if (token.size == 0) {
        ;  // Nothing to do because cur->load_balance_token is an empty string.
      } else if (token.size <= GRPC_GRPCLB_SERVER_LOAD_BALANCE_TOKEN_MAX_SIZE) {
        memcpy(cur.load_balance_token, token.data, token.size);
      } else {
        gpr_log(GPR_ERROR,
                "grpc_lb_v1_LoadBalanceResponse has too long token. len=%zu",
                token.size);
      }
      cur.drop = grpc_lb_v1_Server_drop(servers[i]);
    }
  }
  return true;
}

Duration ParseDuration(const google_protobuf_Duration* duration_pb) {
  return Duration::FromSecondsAndNanoseconds(
      google_protobuf_Duration_seconds(duration_pb),
      google_protobuf_Duration_nanos(duration_pb));
}

}  // namespace

bool GrpcLbResponseParse(const grpc_slice& serialized_response,
                         upb_Arena* arena, GrpcLbResponse* result) {
  grpc_lb_v1_LoadBalanceResponse* response =
      grpc_lb_v1_LoadBalanceResponse_parse(
          reinterpret_cast<const char*>(
              GRPC_SLICE_START_PTR(serialized_response)),
          GRPC_SLICE_LENGTH(serialized_response), arena);
  // Handle serverlist responses.
  if (ParseServerList(*response, &result->serverlist)) {
    result->type = result->SERVERLIST;
    return true;
  }
  // Handle initial responses.
  auto* initial_response =
      grpc_lb_v1_LoadBalanceResponse_initial_response(response);
  if (initial_response != nullptr) {
    result->type = result->INITIAL;
    const google_protobuf_Duration* client_stats_report_interval =
        grpc_lb_v1_InitialLoadBalanceResponse_client_stats_report_interval(
            initial_response);
    if (client_stats_report_interval != nullptr) {
      result->client_stats_report_interval =
          ParseDuration(client_stats_report_interval);
    }
    return true;
  }
  // Handle fallback.
  if (grpc_lb_v1_LoadBalanceResponse_has_fallback_response(response)) {
    result->type = result->FALLBACK;
    return true;
  }
  // Unknown response type.
  return false;
}

}  // namespace grpc_core
