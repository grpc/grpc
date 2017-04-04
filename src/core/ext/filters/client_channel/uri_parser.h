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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_URI_PARSER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_URI_PARSER_H

#include <stddef.h>
#include "src/core/lib/iomgr/exec_ctx.h"

typedef struct {
  char *scheme;
  char *authority;
  char *path;
  char *query;
  /** Query substrings separated by '&' */
  char **query_parts;
  /** Number of elements in \a query_parts and \a query_parts_values */
  size_t num_query_parts;
  /** Split each query part by '='. NULL if not present. */
  char **query_parts_values;
  char *fragment;
} grpc_uri;

/** parse a uri, return NULL on failure */
grpc_uri *grpc_uri_parse(grpc_exec_ctx *exec_ctx, const char *uri_text,
                         int suppress_errors);

/** return the part of a query string after the '=' in "?key=xxx&...", or NULL
 * if key is not present */
const char *grpc_uri_get_query_arg(const grpc_uri *uri, const char *key);

/** destroy a uri */
void grpc_uri_destroy(grpc_uri *uri);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_URI_PARSER_H */
