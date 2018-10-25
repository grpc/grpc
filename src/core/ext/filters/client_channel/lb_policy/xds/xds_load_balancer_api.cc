/*
 *
 * Copyright 2018 gRPC authors.
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

#include "pb_decode.h"
#include "pb_encode.h"
#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_load_balancer_api.h"

#include <grpc/support/alloc.h>

/* invoked once for every Server in ServerList */
static bool count_serverlist(pb_istream_t* stream, const pb_field_t* field,
                             void** arg) {
  xds_grpclb_serverlist* sl = static_cast<xds_grpclb_serverlist*>(*arg);
  xds_grpclb_server server;
  if (GPR_UNLIKELY(!pb_decode(stream, grpc_lb_v1_Server_fields, &server))) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(stream));
    return false;
  }
  ++sl->num_servers;
  return true;
}

typedef struct decode_serverlist_arg {
  /* The decoding callback is invoked once per server in serverlist. Remember
   * which index of the serverlist are we currently decoding */
  size_t decoding_idx;
  /* The decoded serverlist */
  xds_grpclb_serverlist* serverlist;
} decode_serverlist_arg;

/* invoked once for every Server in ServerList */
static bool decode_serverlist(pb_istream_t* stream, const pb_field_t* field,
                              void** arg) {
  decode_serverlist_arg* dec_arg = static_cast<decode_serverlist_arg*>(*arg);
  GPR_ASSERT(dec_arg->serverlist->num_servers >= dec_arg->decoding_idx);
  xds_grpclb_server* server =
      static_cast<xds_grpclb_server*>(gpr_zalloc(sizeof(xds_grpclb_server)));
  if (GPR_UNLIKELY(!pb_decode(stream, grpc_lb_v1_Server_fields, server))) {
    gpr_free(server);
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(stream));
    return false;
  }
  dec_arg->serverlist->servers[dec_arg->decoding_idx++] = server;
  return true;
}

xds_grpclb_request* xds_grpclb_request_create(const char* lb_service_name) {
  xds_grpclb_request* req =
      static_cast<xds_grpclb_request*>(gpr_malloc(sizeof(xds_grpclb_request)));
  req->has_client_stats = false;
  req->has_initial_request = true;
  req->initial_request.has_name = true;
  strncpy(req->initial_request.name, lb_service_name,
          XDS_SERVICE_NAME_MAX_LENGTH);
  return req;
}

static void populate_timestamp(gpr_timespec timestamp,
                               xds_grpclb_timestamp* timestamp_pb) {
  timestamp_pb->has_seconds = true;
  timestamp_pb->seconds = timestamp.tv_sec;
  timestamp_pb->has_nanos = true;
  timestamp_pb->nanos = timestamp.tv_nsec;
}

static bool encode_string(pb_ostream_t* stream, const pb_field_t* field,
                          void* const* arg) {
  char* str = static_cast<char*>(*arg);
  if (!pb_encode_tag_for_field(stream, field)) return false;
  return pb_encode_string(stream, reinterpret_cast<uint8_t*>(str), strlen(str));
}

static bool encode_drops(pb_ostream_t* stream, const pb_field_t* field,
                         void* const* arg) {
  grpc_core::XdsLbClientStats::DroppedCallCounts* drop_entries =
      static_cast<grpc_core::XdsLbClientStats::DroppedCallCounts*>(*arg);
  if (drop_entries == nullptr) return true;
  for (size_t i = 0; i < drop_entries->size(); ++i) {
    if (!pb_encode_tag_for_field(stream, field)) return false;
    grpc_lb_v1_ClientStatsPerToken drop_message;
    drop_message.load_balance_token.funcs.encode = encode_string;
    drop_message.load_balance_token.arg = (*drop_entries)[i].token.get();
    drop_message.has_num_calls = true;
    drop_message.num_calls = (*drop_entries)[i].count;
    if (!pb_encode_submessage(stream, grpc_lb_v1_ClientStatsPerToken_fields,
                              &drop_message)) {
      return false;
    }
  }
  return true;
}

xds_grpclb_request* xds_grpclb_load_report_request_create_locked(
    grpc_core::XdsLbClientStats* client_stats) {
  xds_grpclb_request* req =
      static_cast<xds_grpclb_request*>(gpr_zalloc(sizeof(xds_grpclb_request)));
  req->has_client_stats = true;
  req->client_stats.has_timestamp = true;
  populate_timestamp(gpr_now(GPR_CLOCK_REALTIME), &req->client_stats.timestamp);
  req->client_stats.has_num_calls_started = true;
  req->client_stats.has_num_calls_finished = true;
  req->client_stats.has_num_calls_finished_with_client_failed_to_send = true;
  req->client_stats.has_num_calls_finished_with_client_failed_to_send = true;
  req->client_stats.has_num_calls_finished_known_received = true;
  req->client_stats.calls_finished_with_drop.funcs.encode = encode_drops;
  grpc_core::UniquePtr<grpc_core::XdsLbClientStats::DroppedCallCounts>
      drop_counts;
  client_stats->GetLocked(
      &req->client_stats.num_calls_started,
      &req->client_stats.num_calls_finished,
      &req->client_stats.num_calls_finished_with_client_failed_to_send,
      &req->client_stats.num_calls_finished_known_received, &drop_counts);
  // Will be deleted in xds_grpclb_request_destroy().
  req->client_stats.calls_finished_with_drop.arg = drop_counts.release();
  return req;
}

grpc_slice xds_grpclb_request_encode(const xds_grpclb_request* request) {
  size_t encoded_length;
  pb_ostream_t sizestream;
  pb_ostream_t outputstream;
  grpc_slice slice;
  memset(&sizestream, 0, sizeof(pb_ostream_t));
  pb_encode(&sizestream, grpc_lb_v1_LoadBalanceRequest_fields, request);
  encoded_length = sizestream.bytes_written;

  slice = GRPC_SLICE_MALLOC(encoded_length);
  outputstream =
      pb_ostream_from_buffer(GRPC_SLICE_START_PTR(slice), encoded_length);
  GPR_ASSERT(pb_encode(&outputstream, grpc_lb_v1_LoadBalanceRequest_fields,
                       request) != 0);
  return slice;
}

void xds_grpclb_request_destroy(xds_grpclb_request* request) {
  if (request->has_client_stats) {
    grpc_core::XdsLbClientStats::DroppedCallCounts* drop_entries =
        static_cast<grpc_core::XdsLbClientStats::DroppedCallCounts*>(
            request->client_stats.calls_finished_with_drop.arg);
    grpc_core::Delete(drop_entries);
  }
  gpr_free(request);
}

typedef grpc_lb_v1_LoadBalanceResponse xds_grpclb_response;
xds_grpclb_initial_response* xds_grpclb_initial_response_parse(
    grpc_slice encoded_xds_grpclb_response) {
  pb_istream_t stream =
      pb_istream_from_buffer(GRPC_SLICE_START_PTR(encoded_xds_grpclb_response),
                             GRPC_SLICE_LENGTH(encoded_xds_grpclb_response));
  xds_grpclb_response res;
  memset(&res, 0, sizeof(xds_grpclb_response));
  if (GPR_UNLIKELY(
          !pb_decode(&stream, grpc_lb_v1_LoadBalanceResponse_fields, &res))) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
    return nullptr;
  }

  if (!res.has_initial_response) return nullptr;

  xds_grpclb_initial_response* initial_res =
      static_cast<xds_grpclb_initial_response*>(
          gpr_malloc(sizeof(xds_grpclb_initial_response)));
  memcpy(initial_res, &res.initial_response,
         sizeof(xds_grpclb_initial_response));

  return initial_res;
}

xds_grpclb_serverlist* xds_grpclb_response_parse_serverlist(
    grpc_slice encoded_xds_grpclb_response) {
  pb_istream_t stream =
      pb_istream_from_buffer(GRPC_SLICE_START_PTR(encoded_xds_grpclb_response),
                             GRPC_SLICE_LENGTH(encoded_xds_grpclb_response));
  pb_istream_t stream_at_start = stream;
  xds_grpclb_serverlist* sl = static_cast<xds_grpclb_serverlist*>(
      gpr_zalloc(sizeof(xds_grpclb_serverlist)));
  xds_grpclb_response res;
  memset(&res, 0, sizeof(xds_grpclb_response));
  // First pass: count number of servers.
  res.server_list.servers.funcs.decode = count_serverlist;
  res.server_list.servers.arg = sl;
  bool status = pb_decode(&stream, grpc_lb_v1_LoadBalanceResponse_fields, &res);
  if (GPR_UNLIKELY(!status)) {
    gpr_free(sl);
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
    return nullptr;
  }
  // Second pass: populate servers.
  if (sl->num_servers > 0) {
    sl->servers = static_cast<xds_grpclb_server**>(
        gpr_zalloc(sizeof(xds_grpclb_server*) * sl->num_servers));
    decode_serverlist_arg decode_arg;
    memset(&decode_arg, 0, sizeof(decode_arg));
    decode_arg.serverlist = sl;
    res.server_list.servers.funcs.decode = decode_serverlist;
    res.server_list.servers.arg = &decode_arg;
    status = pb_decode(&stream_at_start, grpc_lb_v1_LoadBalanceResponse_fields,
                       &res);
    if (GPR_UNLIKELY(!status)) {
      xds_grpclb_destroy_serverlist(sl);
      gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
      return nullptr;
    }
  }
  return sl;
}

void xds_grpclb_destroy_serverlist(xds_grpclb_serverlist* serverlist) {
  if (serverlist == nullptr) {
    return;
  }
  for (size_t i = 0; i < serverlist->num_servers; i++) {
    gpr_free(serverlist->servers[i]);
  }
  gpr_free(serverlist->servers);
  gpr_free(serverlist);
}

xds_grpclb_serverlist* xds_grpclb_serverlist_copy(
    const xds_grpclb_serverlist* sl) {
  xds_grpclb_serverlist* copy = static_cast<xds_grpclb_serverlist*>(
      gpr_zalloc(sizeof(xds_grpclb_serverlist)));
  copy->num_servers = sl->num_servers;
  copy->servers = static_cast<xds_grpclb_server**>(
      gpr_malloc(sizeof(xds_grpclb_server*) * sl->num_servers));
  for (size_t i = 0; i < sl->num_servers; i++) {
    copy->servers[i] =
        static_cast<xds_grpclb_server*>(gpr_malloc(sizeof(xds_grpclb_server)));
    memcpy(copy->servers[i], sl->servers[i], sizeof(xds_grpclb_server));
  }
  return copy;
}

bool xds_grpclb_serverlist_equals(const xds_grpclb_serverlist* lhs,
                                  const xds_grpclb_serverlist* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  if (lhs->num_servers != rhs->num_servers) {
    return false;
  }
  for (size_t i = 0; i < lhs->num_servers; i++) {
    if (!xds_grpclb_server_equals(lhs->servers[i], rhs->servers[i])) {
      return false;
    }
  }
  return true;
}

bool xds_grpclb_server_equals(const xds_grpclb_server* lhs,
                              const xds_grpclb_server* rhs) {
  return memcmp(lhs, rhs, sizeof(xds_grpclb_server)) == 0;
}

int xds_grpclb_duration_compare(const xds_grpclb_duration* lhs,
                                const xds_grpclb_duration* rhs) {
  GPR_ASSERT(lhs && rhs);
  if (lhs->has_seconds && rhs->has_seconds) {
    if (lhs->seconds < rhs->seconds) return -1;
    if (lhs->seconds > rhs->seconds) return 1;
  } else if (lhs->has_seconds) {
    return 1;
  } else if (rhs->has_seconds) {
    return -1;
  }

  GPR_ASSERT(lhs->seconds == rhs->seconds);
  if (lhs->has_nanos && rhs->has_nanos) {
    if (lhs->nanos < rhs->nanos) return -1;
    if (lhs->nanos > rhs->nanos) return 1;
  } else if (lhs->has_nanos) {
    return 1;
  } else if (rhs->has_nanos) {
    return -1;
  }

  return 0;
}

grpc_millis xds_grpclb_duration_to_millis(xds_grpclb_duration* duration_pb) {
  return static_cast<grpc_millis>(
      (duration_pb->has_seconds ? duration_pb->seconds : 0) * GPR_MS_PER_SEC +
      (duration_pb->has_nanos ? duration_pb->nanos : 0) / GPR_NS_PER_MS);
}

void xds_grpclb_initial_response_destroy(
    xds_grpclb_initial_response* response) {
  gpr_free(response);
}
