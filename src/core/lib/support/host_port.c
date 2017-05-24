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

#include <grpc/support/host_port.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/support/string.h"

int gpr_join_host_port(char **out, const char *host, int port) {
  if (host[0] != '[' && strchr(host, ':') != NULL) {
    /* IPv6 literals must be enclosed in brackets. */
    return gpr_asprintf(out, "[%s]:%d", host, port);
  } else {
    /* Ordinary non-bracketed host:port. */
    return gpr_asprintf(out, "%s:%d", host, port);
  }
}

int gpr_split_host_port(const char *name, char **host, char **port) {
  const char *host_start;
  size_t host_len;
  const char *port_start;

  *host = NULL;
  *port = NULL;

  if (name[0] == '[') {
    /* Parse a bracketed host, typically an IPv6 literal. */
    const char *rbracket = strchr(name, ']');
    if (rbracket == NULL) {
      /* Unmatched [ */
      return 0;
    }
    if (rbracket[1] == '\0') {
      /* ]<end> */
      port_start = NULL;
    } else if (rbracket[1] == ':') {
      /* ]:<port?> */
      port_start = rbracket + 2;
    } else {
      /* ]<invalid> */
      return 0;
    }
    host_start = name + 1;
    host_len = (size_t)(rbracket - host_start);
    if (memchr(host_start, ':', host_len) == NULL) {
      /* Require all bracketed hosts to contain a colon, because a hostname or
         IPv4 address should never use brackets. */
      return 0;
    }
  } else {
    const char *colon = strchr(name, ':');
    if (colon != NULL && strchr(colon + 1, ':') == NULL) {
      /* Exactly 1 colon.  Split into host:port. */
      host_start = name;
      host_len = (size_t)(colon - name);
      port_start = colon + 1;
    } else {
      /* 0 or 2+ colons.  Bare hostname or IPv6 litearal. */
      host_start = name;
      host_len = strlen(name);
      port_start = NULL;
    }
  }

  /* Allocate return values. */
  *host = (char *)gpr_malloc(host_len + 1);
  memcpy(*host, host_start, host_len);
  (*host)[host_len] = '\0';

  if (port_start != NULL) {
    *port = gpr_strdup(port_start);
  }

  return 1;
}
