/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/ext/lb_policy/grpclb/load_balancer_api.h"
#include "third_party/nanopb/pb_decode.h"
#include "third_party/nanopb/pb_encode.h"

#include <grpc/support/alloc.h>

typedef struct decode_serverlist_arg {
  /* The first pass counts the number of servers in the server list. The second
   * one allocates and decodes. */
  bool first_pass;
  /* The decoding callback is invoked once per server in serverlist. Remember
   * which index of the serverlist are we currently decoding */
  size_t decoding_idx;
  /* Populated after the first pass. Number of server in the input serverlist */
  size_t num_servers;
  /* The decoded serverlist */
  grpc_grpclb_server **servers;
} decode_serverlist_arg;

/* invoked once for every Server in ServerList */
static bool decode_serverlist(pb_istream_t *stream, const pb_field_t *field,
                              void **arg) {
  decode_serverlist_arg *dec_arg = *arg;
  if (dec_arg->first_pass) { /* count how many server do we have */
    grpc_grpclb_server server;
    if (!pb_decode(stream, grpc_lb_v1_Server_fields, &server)) {
      gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(stream));
      return false;
    }
    dec_arg->num_servers++;
  } else { /* second pass. Actually decode. */
    grpc_grpclb_server *server = gpr_zalloc(sizeof(grpc_grpclb_server));
    GPR_ASSERT(dec_arg->num_servers > 0);
    if (dec_arg->decoding_idx == 0) { /* first iteration of second pass */
      dec_arg->servers =
          gpr_malloc(sizeof(grpc_grpclb_server *) * dec_arg->num_servers);
    }
    if (!pb_decode(stream, grpc_lb_v1_Server_fields, server)) {
      gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(stream));
      return false;
    }
    dec_arg->servers[dec_arg->decoding_idx++] = server;
  }

  return true;
}

grpc_grpclb_request *grpc_grpclb_request_create(const char *lb_service_name) {
  grpc_grpclb_request *req = gpr_malloc(sizeof(grpc_grpclb_request));

  req->has_client_stats = 0; /* TODO(dgq): add support for stats once defined */
  req->has_initial_request = 1;
  req->initial_request.has_name = 1;
  strncpy(req->initial_request.name, lb_service_name,
          GRPC_GRPCLB_SERVICE_NAME_MAX_LENGTH);
  return req;
}

grpc_slice grpc_grpclb_request_encode(const grpc_grpclb_request *request) {
  size_t encoded_length;
  pb_ostream_t sizestream;
  pb_ostream_t outputstream;
  grpc_slice slice;
  memset(&sizestream, 0, sizeof(pb_ostream_t));
  pb_encode(&sizestream, grpc_lb_v1_LoadBalanceRequest_fields, request);
  encoded_length = sizestream.bytes_written;

  slice = grpc_slice_malloc(encoded_length);
  outputstream =
      pb_ostream_from_buffer(GRPC_SLICE_START_PTR(slice), encoded_length);
  GPR_ASSERT(pb_encode(&outputstream, grpc_lb_v1_LoadBalanceRequest_fields,
                       request) != 0);
  return slice;
}

void grpc_grpclb_request_destroy(grpc_grpclb_request *request) {
  gpr_free(request);
}

typedef grpc_lb_v1_LoadBalanceResponse grpc_grpclb_response;
grpc_grpclb_initial_response *grpc_grpclb_initial_response_parse(
    grpc_slice encoded_grpc_grpclb_response) {
  pb_istream_t stream =
      pb_istream_from_buffer(GRPC_SLICE_START_PTR(encoded_grpc_grpclb_response),
                             GRPC_SLICE_LENGTH(encoded_grpc_grpclb_response));
  grpc_grpclb_response res;
  memset(&res, 0, sizeof(grpc_grpclb_response));
  if (!pb_decode(&stream, grpc_lb_v1_LoadBalanceResponse_fields, &res)) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
    return NULL;
  }
  grpc_grpclb_initial_response *initial_res =
      gpr_malloc(sizeof(grpc_grpclb_initial_response));
  memcpy(initial_res, &res.initial_response,
         sizeof(grpc_grpclb_initial_response));

  return initial_res;
}

grpc_grpclb_serverlist *grpc_grpclb_response_parse_serverlist(
    grpc_slice encoded_grpc_grpclb_response) {
  bool status;
  decode_serverlist_arg arg;
  pb_istream_t stream =
      pb_istream_from_buffer(GRPC_SLICE_START_PTR(encoded_grpc_grpclb_response),
                             GRPC_SLICE_LENGTH(encoded_grpc_grpclb_response));
  pb_istream_t stream_at_start = stream;
  grpc_grpclb_response res;
  memset(&res, 0, sizeof(grpc_grpclb_response));
  memset(&arg, 0, sizeof(decode_serverlist_arg));

  res.server_list.servers.funcs.decode = decode_serverlist;
  res.server_list.servers.arg = &arg;
  arg.first_pass = true;
  status = pb_decode(&stream, grpc_lb_v1_LoadBalanceResponse_fields, &res);
  if (!status) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
    return NULL;
  }

  arg.first_pass = false;
  status =
      pb_decode(&stream_at_start, grpc_lb_v1_LoadBalanceResponse_fields, &res);
  if (!status) {
    gpr_log(GPR_ERROR, "nanopb error: %s", PB_GET_ERROR(&stream));
    return NULL;
  }

  grpc_grpclb_serverlist *sl = gpr_zalloc(sizeof(grpc_grpclb_serverlist));
  sl->num_servers = arg.num_servers;
  sl->servers = arg.servers;
  if (res.server_list.has_expiration_interval) {
    sl->expiration_interval = res.server_list.expiration_interval;
  }
  return sl;
}

void grpc_grpclb_destroy_serverlist(grpc_grpclb_serverlist *serverlist) {
  if (serverlist == NULL) {
    return;
  }
  for (size_t i = 0; i < serverlist->num_servers; i++) {
    gpr_free(serverlist->servers[i]);
  }
  gpr_free(serverlist->servers);
  gpr_free(serverlist);
}

grpc_grpclb_serverlist *grpc_grpclb_serverlist_copy(
    const grpc_grpclb_serverlist *sl) {
  grpc_grpclb_serverlist *copy = gpr_zalloc(sizeof(grpc_grpclb_serverlist));
  copy->num_servers = sl->num_servers;
  memcpy(&copy->expiration_interval, &sl->expiration_interval,
         sizeof(grpc_grpclb_duration));
  copy->servers = gpr_malloc(sizeof(grpc_grpclb_server *) * sl->num_servers);
  for (size_t i = 0; i < sl->num_servers; i++) {
    copy->servers[i] = gpr_malloc(sizeof(grpc_grpclb_server));
    memcpy(copy->servers[i], sl->servers[i], sizeof(grpc_grpclb_server));
  }
  return copy;
}

bool grpc_grpclb_serverlist_equals(const grpc_grpclb_serverlist *lhs,
                                   const grpc_grpclb_serverlist *rhs) {
  if ((lhs == NULL) || (rhs == NULL)) {
    return false;
  }
  if (lhs->num_servers != rhs->num_servers) {
    return false;
  }
  if (grpc_grpclb_duration_compare(&lhs->expiration_interval,
                                   &rhs->expiration_interval) != 0) {
    return false;
  }
  for (size_t i = 0; i < lhs->num_servers; i++) {
    if (!grpc_grpclb_server_equals(lhs->servers[i], rhs->servers[i])) {
      return false;
    }
  }
  return true;
}

bool grpc_grpclb_server_equals(const grpc_grpclb_server *lhs,
                               const grpc_grpclb_server *rhs) {
  return memcmp(lhs, rhs, sizeof(grpc_grpclb_server)) == 0;
}

int grpc_grpclb_duration_compare(const grpc_grpclb_duration *lhs,
                                 const grpc_grpclb_duration *rhs) {
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

void grpc_grpclb_initial_response_destroy(
    grpc_grpclb_initial_response *response) {
  gpr_free(response);
}
