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

#include "src/core/ext/filters/client_channel/http_proxy.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/uri_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/support/env.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/slice/b64.h"

static void grpc_get_http_proxy_server(grpc_exec_ctx* exec_ctx,
                                char **name_to_resolve,
                                char **user_cred) {
  *name_to_resolve = NULL;
  char* uri_str = gpr_getenv("http_proxy");
  if (uri_str == NULL) return;
  grpc_uri* uri =
      grpc_uri_parse(exec_ctx, uri_str, false /* suppress_errors */);
  if (uri == NULL || uri->authority == NULL) {
    gpr_log(GPR_ERROR, "cannot parse value of 'http_proxy' env var");
    goto done;
  }
  if (strcmp(uri->scheme, "http") != 0) {
    gpr_log(GPR_ERROR, "'%s' scheme not supported in proxy URI", uri->scheme);
    goto done;
  }
  char *user_cred_end = strchr(uri->authority, '@');
  if (user_cred_end != NULL) {
    *name_to_resolve = gpr_strdup(user_cred_end + 1);
    *user_cred_end = '\0';
    *user_cred = gpr_strdup(uri->authority);
    gpr_log(GPR_INFO, "userinfo found in proxy URI");
  } else {
    *name_to_resolve = gpr_strdup(uri->authority);
  }
done:
  gpr_free(uri_str);
  grpc_uri_destroy(uri);
}

static bool proxy_mapper_map_name(grpc_exec_ctx* exec_ctx,
                                  grpc_proxy_mapper* mapper,
                                  const char* server_uri,
                                  const grpc_channel_args* args,
                                  char** name_to_resolve,
                                  grpc_channel_args** new_args) {
  char *user_cred = NULL;
  grpc_get_http_proxy_server(exec_ctx, name_to_resolve, &user_cred);
  if (*name_to_resolve == NULL) return false;
  grpc_uri* uri =
      grpc_uri_parse(exec_ctx, server_uri, false /* suppress_errors */);
  if (uri == NULL || uri->path[0] == '\0') {
    gpr_log(GPR_ERROR,
            "'http_proxy' environment variable set, but cannot "
            "parse server URI '%s' -- not using proxy",
            server_uri);
    if (uri != NULL) {
      gpr_free(user_cred);
      grpc_uri_destroy(uri);
    }
    return false;
  }
  if (strcmp(uri->scheme, "unix") == 0) {
    gpr_log(GPR_INFO, "not using proxy for Unix domain socket '%s'",
            server_uri);
    gpr_free(user_cred);
    grpc_uri_destroy(uri);
    return false;
  }

  grpc_arg args_to_add[2];
  args_to_add[0] = grpc_channel_arg_string_create(
      GRPC_ARG_HTTP_CONNECT_SERVER,
      uri->path[0] == '/' ? uri->path + 1 : uri->path);

  if(user_cred != NULL) {
    /* Use base64 encoding for user credentials */
    char *encoded_user_cred =
        grpc_base64_encode(user_auth, strlen(user_cred), 0, 0);
    char *header;
    gpr_asprintf(&header, "Proxy-Authorization:Basic %s", encoded_user_cred);
    gpr_free(encoded_user_cred);
    args_to_add[1] = grpc_channel_arg_string_create(
        GRPC_ARG_HTTP_CONNECT_HEADERS, header);
    *new_args = grpc_channel_args_copy_and_add(args, args_to_add, 2);
    gpr_free(header);
  } else {
    *new_args = grpc_channel_args_copy_and_add(args, args_to_add, 1);
  }
  gpr_free(user_cred);
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
