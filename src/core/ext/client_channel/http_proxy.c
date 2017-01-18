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

#include "src/core/ext/client_channel/uri_parser.h"
#include "src/core/lib/support/env.h"

char* grpc_get_http_proxy_server() {
  char* uri_str = gpr_getenv("http_proxy");
  if (uri_str == NULL) return NULL;
  grpc_uri* uri = grpc_uri_parse(uri_str, false /* suppress_errors */);
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
