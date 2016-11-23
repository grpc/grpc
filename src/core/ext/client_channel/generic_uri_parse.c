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

#include "src/core/ext/client_channel/generic_uri_parse.h"

#include <string.h>
#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/host_port.h>

#define MAX_HOST_PORT_PARSERS 2

static grpc_host_port_parser *g_all_of_the_host_port_parsers[MAX_HOST_PORT_PARSERS];
static int g_number_of_host_port_parsers = 0;

void default_host_port_parser_ref(grpc_host_port_parser *parser);
void default_host_port_parser_unref(grpc_host_port_parser *parser);
int default_host_port_parser_join_host_port(grpc_host_port_parser *parser, char **joined_host_port, const char *host, const char *port);
int default_host_port_parser_split_host_port(grpc_host_port_parser *parser, const char *joined_host_port, char **host, char **port);

static const grpc_host_port_parser_vtable default_host_port_parser_vtable = {
  default_host_port_parser_ref,
  default_host_port_parser_unref,
  default_host_port_parser_join_host_port,
  default_host_port_parser_split_host_port,
  "",
};

static grpc_host_port_parser default_host_port_parser =  { &default_host_port_parser_vtable };

void grpc_host_port_parser_ref(grpc_host_port_parser* parser) {
  parser->vtable->ref(parser);
}

void grpc_host_port_parser_unref(grpc_host_port_parser* parser) {
  parser->vtable->unref(parser);
}

int grpc_host_port_parser_join_host_port(
    grpc_host_port_parser* parser, char **joined_host_port, const char *host, const char *port) {
  if (parser == NULL) return -1;
  return parser->vtable->join_host_port(parser, joined_host_port, host, port);
}

int grpc_host_port_parser_split_host_port(
    grpc_host_port_parser* parser, const char *joined_host_port, char **host, char **port) {
  if (parser == NULL) return -1;
  return parser->vtable->split_host_port(parser, joined_host_port, host, port);
}

void default_host_port_parser_ref(grpc_host_port_parser *parser) {
  gpr_log(GPR_INFO, "hello from default ref");
}

void default_host_port_parser_unref(grpc_host_port_parser *parser) {
  gpr_log(GPR_INFO, "hello from default unref");
}

int default_host_port_parser_join_host_port(grpc_host_port_parser *parser, char **joined_host_port, const char *host, const char *port) {
  gpr_log(GPR_INFO, "hello from default join");
  long numeric_port;
  char *endptr;

  GPR_ASSERT(strlen(port) > 0);
  numeric_port = strtol(port, &endptr, 10);
  GPR_ASSERT(*endptr == 0);

  return gpr_join_host_port(joined_host_port, host, (int)numeric_port);
}

int default_host_port_parser_split_host_port(grpc_host_port_parser *parser, const char *joined_host_port, char **host, char **port) {
  gpr_log(GPR_INFO, "hello from default split");
  return gpr_split_host_port(joined_host_port, host, port);
}


void grpc_register_host_port_parser(grpc_host_port_parser *parser) {
  int i;
  for (i = 0; i < g_number_of_host_port_parsers; i++) {
    GPR_ASSERT(0 != strcmp(parser->vtable->scheme,
                           g_all_of_the_host_port_parsers[i]->vtable->scheme));
  }
  GPR_ASSERT(g_number_of_host_port_parsers != MAX_HOST_PORT_PARSERS);
  grpc_host_port_parser_ref(parser);
  g_all_of_the_host_port_parsers[g_number_of_host_port_parsers++] = parser;
}

grpc_host_port_parser *lookup_host_port_parser(const char *scheme) {
  int i;

  for (i = 0; i < g_number_of_host_port_parsers; i++) {
    if (0 == strcmp(scheme, g_all_of_the_host_port_parsers[i]->vtable->scheme)) {
      return g_all_of_the_host_port_parsers[i];
    }
  }

  return NULL;
}

grpc_host_port_parser *grpc_host_port_parser_lookup(const char *scheme) {
  grpc_host_port_parser *p = lookup_host_port_parser(scheme);
  if (p) grpc_host_port_parser_ref(p);
  return p;
}

grpc_host_port_parser *resolve_host_port_parser(const char *target) {
  grpc_host_port_parser *parser = NULL;
  grpc_uri *uri = NULL;

  GPR_ASSERT(target != NULL);
  uri = grpc_uri_parse(target, 1);
  if (uri != NULL && uri->scheme != NULL && strlen(uri->scheme) > 0) {
    parser = grpc_host_port_parser_lookup(uri->scheme);
  }
  if (uri != NULL) {
    grpc_uri_destroy(uri);
  }

  if (parser == NULL) {
    return &default_host_port_parser;
  }
  return parser;
}

int grpc_generic_join_host_port(char **joined_host_port, const char *host, const char *port) {
  grpc_host_port_parser *parser = NULL;

  parser = resolve_host_port_parser(host);
  GPR_ASSERT(parser != NULL);
  gpr_log(GPR_INFO, "hello from grpc_generic_join_host_port");
  return grpc_host_port_parser_join_host_port(parser, joined_host_port, host, port);
}

int grpc_generic_split_host_port(const char *joined_host_port, char **host, char **port) {
  grpc_host_port_parser *parser = NULL;

  parser = resolve_host_port_parser(joined_host_port);
  GPR_ASSERT(parser != NULL);
  gpr_log(GPR_INFO, "hello from grpc_generic_split_host_port");
  return grpc_host_port_parser_split_host_port(parser, joined_host_port, host, port);
}

void grpc_generic_host_port_parser_registry_init() { }

void grpc_generic_host_port_parser_registry_shutdown() {
  for (int i = 0; i < g_number_of_host_port_parsers; i++) {
    grpc_host_port_parser_unref(g_all_of_the_host_port_parsers[i]);
  }
}
