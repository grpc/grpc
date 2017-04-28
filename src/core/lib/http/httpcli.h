/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPC_CORE_LIB_HTTP_HTTPCLI_H
#define GRPC_CORE_LIB_HTTP_HTTPCLI_H

#include <stddef.h>

#include <grpc/support/time.h>

#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset_set.h"

/* User agent this library reports */
#define GRPC_HTTPCLI_USER_AGENT "grpc-httpcli/0.0"

/* Tracks in-progress http requests
   TODO(ctiller): allow caching and capturing multiple requests for the
                  same content and combining them */
typedef struct grpc_httpcli_context {
  grpc_pollset_set *pollset_set;
} grpc_httpcli_context;

typedef struct {
  const char *default_port;
  void (*handshake)(grpc_exec_ctx *exec_ctx, void *arg, grpc_endpoint *endpoint,
                    const char *host, gpr_timespec deadline,
                    void (*on_done)(grpc_exec_ctx *exec_ctx, void *arg,
                                    grpc_endpoint *endpoint));
} grpc_httpcli_handshaker;

extern const grpc_httpcli_handshaker grpc_httpcli_plaintext;
extern const grpc_httpcli_handshaker grpc_httpcli_ssl;

/* A request */
typedef struct grpc_httpcli_request {
  /* The host name to connect to */
  char *host;
  /* The host to verify in the SSL handshake (or NULL) */
  char *ssl_host_override;
  /* The main part of the request
     The following headers are supplied automatically and MUST NOT be set here:
     Host, Connection, User-Agent */
  grpc_http_request http;
  /* handshaker to use ssl for the request */
  const grpc_httpcli_handshaker *handshaker;
} grpc_httpcli_request;

/* Expose the parser response type as a httpcli response too */
typedef struct grpc_http_response grpc_httpcli_response;

void grpc_httpcli_context_init(grpc_httpcli_context *context);
void grpc_httpcli_context_destroy(grpc_exec_ctx *exec_ctx,
                                  grpc_httpcli_context *context);

/* Asynchronously perform a HTTP GET.
   'context' specifies the http context under which to do the get
   'pollset' indicates a grpc_pollset that is interested in the result
     of the get - work on this pollset may be used to progress the get
     operation
   'request' contains request parameters - these are caller owned and can be
     destroyed once the call returns
   'deadline' contains a deadline for the request (or gpr_inf_future)
   'on_response' is a callback to report results to */
void grpc_httpcli_get(grpc_exec_ctx *exec_ctx, grpc_httpcli_context *context,
                      grpc_polling_entity *pollent,
                      grpc_resource_quota *resource_quota,
                      const grpc_httpcli_request *request,
                      gpr_timespec deadline, grpc_closure *on_complete,
                      grpc_httpcli_response *response);

/* Asynchronously perform a HTTP POST.
   'context' specifies the http context under which to do the post
   'pollset' indicates a grpc_pollset that is interested in the result
     of the post - work on this pollset may be used to progress the post
     operation
   'request' contains request parameters - these are caller owned and can be
     destroyed once the call returns
   'body_bytes' and 'body_size' specify the payload for the post.
     When there is no body, pass in NULL as body_bytes.
   'deadline' contains a deadline for the request (or gpr_inf_future)
   'em' points to a caller owned event manager that must be alive for the
     lifetime of the request
   'on_response' is a callback to report results to
   Does not support ?var1=val1&var2=val2 in the path. */
void grpc_httpcli_post(grpc_exec_ctx *exec_ctx, grpc_httpcli_context *context,
                       grpc_polling_entity *pollent,
                       grpc_resource_quota *resource_quota,
                       const grpc_httpcli_request *request,
                       const char *body_bytes, size_t body_size,
                       gpr_timespec deadline, grpc_closure *on_complete,
                       grpc_httpcli_response *response);

/* override functions return 1 if they handled the request, 0 otherwise */
typedef int (*grpc_httpcli_get_override)(grpc_exec_ctx *exec_ctx,
                                         const grpc_httpcli_request *request,
                                         gpr_timespec deadline,
                                         grpc_closure *on_complete,
                                         grpc_httpcli_response *response);
typedef int (*grpc_httpcli_post_override)(
    grpc_exec_ctx *exec_ctx, const grpc_httpcli_request *request,
    const char *body_bytes, size_t body_size, gpr_timespec deadline,
    grpc_closure *on_complete, grpc_httpcli_response *response);

void grpc_httpcli_set_override(grpc_httpcli_get_override get,
                               grpc_httpcli_post_override post);

#endif /* GRPC_CORE_LIB_HTTP_HTTPCLI_H */
