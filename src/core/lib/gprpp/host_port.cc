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

#include "src/core/lib/gprpp/host_port.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/string_view.h"

namespace grpc_core {
int JoinHostPort(UniquePtr<char>* out, const char* host, int port) {
  char* tmp;
  int ret;
  if (host[0] != '[' && strchr(host, ':') != nullptr) {
    /* IPv6 literals must be enclosed in brackets. */
    ret = gpr_asprintf(&tmp, "[%s]:%d", host, port);
  } else {
    /* Ordinary non-bracketed host:port. */
    ret = gpr_asprintf(&tmp, "%s:%d", host, port);
  }
  out->reset(tmp);
  return ret;
}

bool SplitHostPort(StringView name, StringView* host, StringView* port) {
  if (name[0] == '[') {
    /* Parse a bracketed host, typically an IPv6 literal. */
    const size_t rbracket = name.find(']', 1);
    if (rbracket == grpc_core::StringView::npos) {
      /* Unmatched [ */
      return false;
    }
    if (rbracket == name.size() - 1) {
      /* ]<end> */
      port->clear();
    } else if (name[rbracket + 1] == ':') {
      /* ]:<port?> */
      *port = name.substr(rbracket + 2, name.size() - rbracket - 2);
    } else {
      /* ]<invalid> */
      return false;
    }
    *host = name.substr(1, rbracket - 1);
    if (host->find(':') == grpc_core::StringView::npos) {
      /* Require all bracketed hosts to contain a colon, because a hostname or
         IPv4 address should never use brackets. */
      host->clear();
      return false;
    }
  } else {
    size_t colon = name.find(':');
    if (colon != grpc_core::StringView::npos &&
        name.find(':', colon + 1) == grpc_core::StringView::npos) {
      /* Exactly 1 colon.  Split into host:port. */
      *host = name.substr(0, colon);
      *port = name.substr(colon + 1, name.size() - colon - 1);
    } else {
      /* 0 or 2+ colons.  Bare hostname or IPv6 litearal. */
      *host = name;
      port->clear();
    }
  }
  return true;
}

bool SplitHostPort(StringView name, UniquePtr<char>* host,
                   UniquePtr<char>* port) {
  StringView host_view;
  StringView port_view;
  const bool ret = SplitHostPort(name, &host_view, &port_view);
  host->reset(host_view.empty() ? nullptr : host_view.dup().release());
  port->reset(port_view.empty() ? nullptr : port_view.dup().release());
  return ret;
}
}  // namespace grpc_core
