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

#include "src/core/ext/client_channel/http_proxy.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/client_channel/http_connect_handshaker.h"
#include "src/core/ext/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/client_channel/uri_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/support/env.h"

static char* grpc_get_http_proxy_server(grpc_exec_ctx* exec_ctx) {
  char* uri_str = gpr_getenv("http_proxy");
  if (uri_str == NULL) return NULL;
  grpc_uri* uri =
      grpc_uri_parse(exec_ctx, uri_str, false /* suppress_errors */);
  char* proxy_name = NULL;
  if (uri == NULL || uri->authority == NULL) {
    gpr_log(GPR_ERROR, "cannot parse value of 'http_proxy' env var");
    goto done;
  }
  if (strcmp(uri->scheme, "http") != 0) {
    gpr_log(GPR_ERROR, "'%s' scheme not supported in proxy URI", uri->scheme);
    goto done;
  }
  if (strchr(uri->authority, '@') != NULL) {
    gpr_log(GPR_ERROR, "userinfo not supported in proxy URI");
    goto done;
  }
  proxy_name = gpr_strdup(uri->authority);
done:
  gpr_free(uri_str);
  grpc_uri_destroy(uri);
  return proxy_name;
}

static bool proxy_mapper_map_name(grpc_exec_ctx* exec_ctx,
                                  grpc_proxy_mapper* mapper,
                                  const char* server_uri,
                                  const grpc_channel_args* args,
                                  char** name_to_resolve,
                                  grpc_channel_args** new_args) {
  *name_to_resolve = grpc_get_http_proxy_server(exec_ctx);
  if (*name_to_resolve == NULL) return false;
  grpc_uri* uri =
      grpc_uri_parse(exec_ctx, server_uri, false /* suppress_errors */);
  if (uri == NULL || uri->path[0] == '\0') {
    gpr_log(GPR_ERROR,
            "'http_proxy' environment variable set, but cannot "
            "parse server URI '%s' -- not using proxy",
            server_uri);
    if (uri != NULL) grpc_uri_destroy(uri);
    return false;
  }
  if (strcmp(uri->scheme, "unix") == 0) {
    gpr_log(GPR_INFO, "not using proxy for Unix domain socket '%s'",
            server_uri);
    grpc_uri_destroy(uri);
    return false;
  }
  grpc_arg new_arg;
  new_arg.key = GRPC_ARG_HTTP_CONNECT_SERVER;
  new_arg.type = GRPC_ARG_STRING;
  new_arg.value.string = uri->path[0] == '/' ? uri->path + 1 : uri->path;
  *new_args = grpc_channel_args_copy_and_add(args, &new_arg, 1);
  grpc_uri_destroy(uri);
  return true;
}

static bool proxy_mapper_map_address(grpc_exec_ctx* exec_ctx,
                                     grpc_proxy_mapper* mapper,
                                     const grpc_resolved_address* address,
                                     const grpc_channel_args* args,
                                     grpc_resolved_address** new_address,
                                     grpc_channel_args** new_args) {
  return false;
}

static void proxy_mapper_destroy(grpc_proxy_mapper* mapper) {}

static const grpc_proxy_mapper_vtable proxy_mapper_vtable = {
    proxy_mapper_map_name, proxy_mapper_map_address, proxy_mapper_destroy};

static grpc_proxy_mapper proxy_mapper = {&proxy_mapper_vtable};

void grpc_register_http_proxy_mapper() {
  grpc_proxy_mapper_register(true /* at_start */, &proxy_mapper);
}
