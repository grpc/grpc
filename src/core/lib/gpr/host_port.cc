/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/gpr/host_port.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"

int gpr_join_host_port(char** out, const char* host, int port) {
  if (host[0] != '[' && strchr(host, ':') != nullptr) {
    /* IPv6 literals must be enclosed in brackets. */
    return gpr_asprintf(out, "[%s]:%d", host, port);
  } else {
    /* Ordinary non-bracketed host:port. */
    return gpr_asprintf(out, "%s:%d", host, port);
  }
}

int gpr_split_host_port(const char* name, char** host, char** port) {
  const char* host_start;
  size_t host_len;
  const char* port_start;

  *host = nullptr;
  *port = nullptr;

  if (name[0] == '[') {
    /* Parse a bracketed host, typically an IPv6 literal. */
    const char* rbracket = strchr(name, ']');
    if (rbracket == nullptr) {
      /* Unmatched [ */
      return 0;
    }
    if (rbracket[1] == '\0') {
      /* ]<end> */
      port_start = nullptr;
    } else if (rbracket[1] == ':') {
      /* ]:<port?> */
      port_start = rbracket + 2;
    } else {
      /* ]<invalid> */
      return 0;
    }
    host_start = name + 1;
    host_len = static_cast<size_t>(rbracket - host_start);
    if (memchr(host_start, ':', host_len) == nullptr) {
      /* Require all bracketed hosts to contain a colon, because a hostname or
         IPv4 address should never use brackets. */
      return 0;
    }
  } else {
    const char* colon = strchr(name, ':');
    if (colon != nullptr && strchr(colon + 1, ':') == nullptr) {
      /* Exactly 1 colon.  Split into host:port. */
      host_start = name;
      host_len = static_cast<size_t>(colon - name);
      port_start = colon + 1;
    } else {
      /* 0 or 2+ colons.  Bare hostname or IPv6 litearal. */
      host_start = name;
      host_len = strlen(name);
      port_start = nullptr;
    }
  }

  /* Allocate return values. */
  *host = static_cast<char*>(gpr_malloc(host_len + 1));
  memcpy(*host, host_start, host_len);
  (*host)[host_len] = '\0';

  if (port_start != nullptr) {
    *port = gpr_strdup(port_start);
  }

  return 1;
}
