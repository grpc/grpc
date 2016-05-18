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
  int first_pass;
  int i;
  size_t num_servers;
  grpc_grpclb_server **servers;
} decode_serverlist_arg;

/* invoked once for every Server in ServerList */
static bool decode_serverlist(pb_istream_t *stream, const pb_field_t *field,
                              void **arg) {
  decode_serverlist_arg *dec_arg = *arg;
  if (dec_arg->first_pass != 0) { /* first pass */
    grpc_grpclb_server server;
    if (!pb_decode(stream, grpc_lb_v1_Server_fields, &server)) {
      return false;
    }
    dec_arg->num_servers++;
  } else { /* second pass */
    grpc_grpclb_server *server = gpr_malloc(sizeof(grpc_grpclb_server));
    GPR_ASSERT(dec_arg->num_servers > 0);
    if (dec_arg->i == 0) { /* first iteration of second pass */
      dec_arg->servers =
          gpr_malloc(sizeof(grpc_grpclb_server *) * dec_arg->num_servers);
    }
    if (!pb_decode(stream, grpc_lb_v1_Server_fields, server)) {
      return false;
    }
    dec_arg->servers[dec_arg->i++] = server;
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

gpr_slice grpc_grpclb_request_encode(const grpc_grpclb_request *request) {
  size_t encoded_length;
  pb_ostream_t sizestream;
  pb_ostream_t outputstream;
  gpr_slice slice;
  memset(&sizestream, 0, sizeof(pb_ostream_t));
  pb_encode(&sizestream, grpc_lb_v1_LoadBalanceRequest_fields, request);
  encoded_length = sizestream.bytes_written;

  slice = gpr_slice_malloc(encoded_length);
  outputstream =
      pb_ostream_from_buffer(GPR_SLICE_START_PTR(slice), encoded_length);
  GPR_ASSERT(pb_encode(&outputstream, grpc_lb_v1_LoadBalanceRequest_fields,
                       request) != 0);
  return slice;
}

void grpc_grpclb_request_destroy(grpc_grpclb_request *request) {
  gpr_free(request);
}

grpc_grpclb_response *grpc_grpclb_response_parse(gpr_slice encoded_response) {
  bool status;
  pb_istream_t stream =
      pb_istream_from_buffer(GPR_SLICE_START_PTR(encoded_response),
                             GPR_SLICE_LENGTH(encoded_response));
  grpc_grpclb_response *res = gpr_malloc(sizeof(grpc_grpclb_response));
  memset(res, 0, sizeof(*res));
  status = pb_decode(&stream, grpc_lb_v1_LoadBalanceResponse_fields, res);
  if (!status) {
    grpc_grpclb_response_destroy(res);
    return NULL;
  }
  return res;
}

grpc_grpclb_serverlist *grpc_grpclb_response_parse_serverlist(
    gpr_slice encoded_response) {
  bool status;
  decode_serverlist_arg arg;
  pb_istream_t stream =
      pb_istream_from_buffer(GPR_SLICE_START_PTR(encoded_response),
                             GPR_SLICE_LENGTH(encoded_response));
  pb_istream_t stream_at_start = stream;
  grpc_grpclb_response *res = gpr_malloc(sizeof(grpc_grpclb_response));
  memset(res, 0, sizeof(*res));
  memset(&arg, 0, sizeof(decode_serverlist_arg));

  res->server_list.servers.funcs.decode = decode_serverlist;
  res->server_list.servers.arg = &arg;
  arg.first_pass = 1;
  status = pb_decode(&stream, grpc_lb_v1_LoadBalanceResponse_fields, res);
  if (!status) {
    grpc_grpclb_response_destroy(res);
    return NULL;
  }

  arg.first_pass = 0;
  status =
      pb_decode(&stream_at_start, grpc_lb_v1_LoadBalanceResponse_fields, res);
  if (!status) {
    grpc_grpclb_response_destroy(res);
    return NULL;
  }

  grpc_grpclb_serverlist *sl = gpr_malloc(sizeof(grpc_grpclb_serverlist));
  sl->num_servers = arg.num_servers;
  sl->servers = arg.servers;
  if (res->server_list.has_expiration_interval) {
    sl->expiration_interval = res->server_list.expiration_interval;
  }
  grpc_grpclb_response_destroy(res);
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

void grpc_grpclb_response_destroy(grpc_grpclb_response *response) {
  gpr_free(response);
}
