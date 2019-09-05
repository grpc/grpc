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

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/load_balancer_api.h"
#include "src/core/lib/gpr/useful.h"

#include "google/protobuf/duration.upb.h"
#include "google/protobuf/timestamp.upb.h"

#include <grpc/support/alloc.h>

namespace grpc_core {

grpc_grpclb_request* grpc_grpclb_request_create(const char* lb_service_name,
                                                upb_arena* arena) {
  grpc_grpclb_request* req = grpc_lb_v1_LoadBalanceRequest_new(arena);
  grpc_lb_v1_InitialLoadBalanceRequest* initial_request =
      grpc_lb_v1_LoadBalanceRequest_mutable_initial_request(req, arena);
  size_t name_len =
      GPR_MIN(strlen(lb_service_name), GRPC_GRPCLB_SERVICE_NAME_MAX_LENGTH);
  grpc_lb_v1_InitialLoadBalanceRequest_set_name(
      initial_request, upb_strview_make(lb_service_name, name_len));
  return req;
}

namespace {

void google_protobuf_Timestamp_assign(google_protobuf_Timestamp* timestamp,
                                      const gpr_timespec& value) {
  google_protobuf_Timestamp_set_seconds(timestamp, value.tv_sec);
  google_protobuf_Timestamp_set_nanos(timestamp, value.tv_nsec);
}

}  // namespace

grpc_grpclb_request* grpc_grpclb_load_report_request_create(
    GrpcLbClientStats* client_stats, upb_arena* arena) {
  grpc_grpclb_request* req = grpc_lb_v1_LoadBalanceRequest_new(arena);
  grpc_lb_v1_ClientStats* req_stats =
      grpc_lb_v1_LoadBalanceRequest_mutable_client_stats(req, arena);
  google_protobuf_Timestamp_assign(
      grpc_lb_v1_ClientStats_mutable_timestamp(req_stats, arena),
      gpr_now(GPR_CLOCK_REALTIME));

  int64_t num_calls_started;
  int64_t num_calls_finished;
  int64_t num_calls_finished_with_client_failed_to_send;
  int64_t num_calls_finished_known_received;
  UniquePtr<GrpcLbClientStats::DroppedCallCounts> drop_token_counts;
  client_stats->Get(&num_calls_started, &num_calls_finished,
                    &num_calls_finished_with_client_failed_to_send,
                    &num_calls_finished_known_received, &drop_token_counts);
  grpc_lb_v1_ClientStats_set_num_calls_started(req_stats, num_calls_started);
  grpc_lb_v1_ClientStats_set_num_calls_finished(req_stats, num_calls_finished);
  grpc_lb_v1_ClientStats_set_num_calls_finished_with_client_failed_to_send(
      req_stats, num_calls_finished_with_client_failed_to_send);
  grpc_lb_v1_ClientStats_set_num_calls_finished_known_received(
      req_stats, num_calls_finished_known_received);
  if (drop_token_counts != nullptr) {
    for (size_t i = 0; i < drop_token_counts->size(); ++i) {
      GrpcLbClientStats::DropTokenCount& cur = (*drop_token_counts)[i];
      grpc_lb_v1_ClientStatsPerToken* cur_msg =
          grpc_lb_v1_ClientStats_add_calls_finished_with_drop(req_stats, arena);

      const size_t token_len = strlen(cur.token.get());
      char* token = reinterpret_cast<char*>(upb_arena_malloc(arena, token_len));
      memcpy(token, cur.token.get(), token_len);

      grpc_lb_v1_ClientStatsPerToken_set_load_balance_token(
          cur_msg, upb_strview_make(token, token_len));
      grpc_lb_v1_ClientStatsPerToken_set_num_calls(cur_msg, cur.count);
    }
  }
  return req;
}

grpc_slice grpc_grpclb_request_encode(const grpc_grpclb_request* request,
                                      upb_arena* arena) {
  size_t buf_length;
  char* buf =
      grpc_lb_v1_LoadBalanceRequest_serialize(request, arena, &buf_length);
  return grpc_slice_from_copied_buffer(buf, buf_length);
}

const grpc_grpclb_initial_response* grpc_grpclb_initial_response_parse(
    const grpc_slice& encoded_grpc_grpclb_response, upb_arena* arena) {
  grpc_lb_v1_LoadBalanceResponse* response =
      grpc_lb_v1_LoadBalanceResponse_parse(
          reinterpret_cast<const char*>(
              GRPC_SLICE_START_PTR(encoded_grpc_grpclb_response)),
          GRPC_SLICE_LENGTH(encoded_grpc_grpclb_response), arena);
  if (response == nullptr) {
    gpr_log(GPR_ERROR, "grpc_lb_v1_LoadBalanceResponse parse error");
    return nullptr;
  }
  return grpc_lb_v1_LoadBalanceResponse_initial_response(response);
}

grpc_grpclb_serverlist* grpc_grpclb_response_parse_serverlist(
    const grpc_slice& encoded_grpc_grpclb_response) {
  upb::Arena arena;
  grpc_lb_v1_LoadBalanceResponse* response =
      grpc_lb_v1_LoadBalanceResponse_parse(
          reinterpret_cast<const char*>(
              GRPC_SLICE_START_PTR(encoded_grpc_grpclb_response)),
          GRPC_SLICE_LENGTH(encoded_grpc_grpclb_response), arena.ptr());
  if (response == nullptr) {
    gpr_log(GPR_ERROR, "grpc_lb_v1_LoadBalanceResponse parse error");
    return nullptr;
  }
  grpc_grpclb_serverlist* server_list = static_cast<grpc_grpclb_serverlist*>(
      gpr_zalloc(sizeof(grpc_grpclb_serverlist)));
  // First pass: count number of servers.
  const grpc_lb_v1_ServerList* server_list_msg =
      grpc_lb_v1_LoadBalanceResponse_server_list(response);
  size_t server_count = 0;
  const grpc_lb_v1_Server* const* servers = nullptr;
  if (server_list_msg != nullptr) {
    servers = grpc_lb_v1_ServerList_servers(server_list_msg, &server_count);
  }
  // Second pass: populate servers.
  if (server_count > 0) {
    server_list->servers = static_cast<grpc_grpclb_server**>(
        gpr_zalloc(sizeof(grpc_grpclb_server*) * server_count));
    server_list->num_servers = server_count;
    for (size_t i = 0; i < server_count; ++i) {
      grpc_grpclb_server* cur = server_list->servers[i] =
          static_cast<grpc_grpclb_server*>(
              gpr_zalloc(sizeof(grpc_grpclb_server)));
      upb_strview address = grpc_lb_v1_Server_ip_address(servers[i]);
      if (address.size == 0) {
        ;  // Nothing to do because cur->ip_address is an empty string.
      } else if (address.size <= GRPC_GRPCLB_SERVER_IP_ADDRESS_MAX_SIZE) {
        cur->ip_address.size = static_cast<int32_t>(address.size);
        memcpy(cur->ip_address.data, address.data, address.size);
      }
      cur->port = grpc_lb_v1_Server_port(servers[i]);
      upb_strview token = grpc_lb_v1_Server_load_balance_token(servers[i]);
      if (token.size == 0) {
        ;  // Nothing to do because cur->load_balance_token is an empty string.
      } else if (token.size <= GRPC_GRPCLB_SERVER_LOAD_BALANCE_TOKEN_MAX_SIZE) {
        memcpy(cur->load_balance_token, token.data, token.size);
      } else {
        gpr_log(GPR_ERROR,
                "grpc_lb_v1_LoadBalanceResponse has too long token. len=%zu",
                token.size);
      }
      cur->drop = grpc_lb_v1_Server_drop(servers[i]);
    }
  }
  return server_list;
}

void grpc_grpclb_destroy_serverlist(grpc_grpclb_serverlist* serverlist) {
  if (serverlist == nullptr) {
    return;
  }
  for (size_t i = 0; i < serverlist->num_servers; i++) {
    gpr_free(serverlist->servers[i]);
  }
  gpr_free(serverlist->servers);
  gpr_free(serverlist);
}

grpc_grpclb_serverlist* grpc_grpclb_serverlist_copy(
    const grpc_grpclb_serverlist* server_list) {
  grpc_grpclb_serverlist* copy = static_cast<grpc_grpclb_serverlist*>(
      gpr_zalloc(sizeof(grpc_grpclb_serverlist)));
  copy->num_servers = server_list->num_servers;
  copy->servers = static_cast<grpc_grpclb_server**>(
      gpr_malloc(sizeof(grpc_grpclb_server*) * server_list->num_servers));
  for (size_t i = 0; i < server_list->num_servers; i++) {
    copy->servers[i] = static_cast<grpc_grpclb_server*>(
        gpr_malloc(sizeof(grpc_grpclb_server)));
    memcpy(copy->servers[i], server_list->servers[i],
           sizeof(grpc_grpclb_server));
  }
  return copy;
}

bool grpc_grpclb_serverlist_equals(const grpc_grpclb_serverlist* lhs,
                                   const grpc_grpclb_serverlist* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  if (lhs->num_servers != rhs->num_servers) {
    return false;
  }
  for (size_t i = 0; i < lhs->num_servers; i++) {
    if (!grpc_grpclb_server_equals(lhs->servers[i], rhs->servers[i])) {
      return false;
    }
  }
  return true;
}

bool grpc_grpclb_server_equals(const grpc_grpclb_server* lhs,
                               const grpc_grpclb_server* rhs) {
  return memcmp(lhs, rhs, sizeof(grpc_grpclb_server)) == 0;
}

grpc_millis grpc_grpclb_duration_to_millis(
    const grpc_grpclb_duration* duration_pb) {
  return static_cast<grpc_millis>(
      google_protobuf_Duration_seconds(duration_pb) * GPR_MS_PER_SEC +
      google_protobuf_Duration_nanos(duration_pb) / GPR_NS_PER_MS);
}

}  // namespace grpc_core
