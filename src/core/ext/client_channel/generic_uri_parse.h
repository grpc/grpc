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

#ifndef GRPC_CORE_EXT_CLIENT_CHANNEL_GENERIC_URI_PARSE_H
#define GRPC_CORE_EXT_CLIENT_CHANNEL_GENERIC_URI_PARSE_H

#include "src/core/ext/client_channel/client_channel_factory.h"
#include "src/core/ext/client_channel/resolver.h"
#include "src/core/ext/client_channel/uri_parser.h"

typedef struct grpc_host_port_parser grpc_host_port_parser;
typedef struct grpc_host_port_parser_vtable grpc_host_port_parser_vtable;

struct grpc_host_port_parser {
  const grpc_host_port_parser_vtable *vtable;
};

struct grpc_host_port_parser_vtable {
  void (*ref)(grpc_host_port_parser *parser);
  void (*unref)(grpc_host_port_parser *parser);

  int (*join_host_port)(grpc_host_port_parser *parser, char **joined_host_port, const char *host, const char *port);
  int (*split_host_port)(grpc_host_port_parser *parser, const char *joined_host_port, char **host, char **port);

  /** URI scheme that this parser implements */
  const char *scheme;
};

void grpc_default_host_port_parser_init(void);
void grpc_default_host_port_parser_shutdown(void);

void grpc_host_port_parser_ref(grpc_host_port_parser *parser);
void grpc_host_port_parser_unref(grpc_host_port_parser *parser);

int grpc_generic_join_host_port(char **joined_host_port, const char *host, const char *port);
int grpc_generic_split_host_port(const char *joined_host_port, char **host, char **port);

void grpc_register_host_port_parser(grpc_host_port_parser *parser);

#endif /* GRPC_CORE_EXT_CLIENT_CHANNEL_GENERIC_URI_PARSE */
