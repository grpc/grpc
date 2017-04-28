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

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"
#include "src/core/lib/security/transport/security_connector.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/util/test_config.h"

void test_unknown_scheme_target(void) {
  grpc_resolver_registry_shutdown();
  grpc_resolver_registry_init();
  grpc_channel_credentials *creds =
      grpc_fake_transport_security_credentials_create();
  grpc_channel *chan =
      grpc_secure_channel_create(creds, "blah://blah", NULL, NULL);
  grpc_channel_element *elem =
      grpc_channel_stack_element(grpc_channel_get_channel_stack(chan), 0);
  GPR_ASSERT(0 == strcmp(elem->filter->name, "lame-client"));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_CHANNEL_INTERNAL_UNREF(&exec_ctx, chan, "test");
  grpc_channel_credentials_unref(&exec_ctx, creds);
  grpc_exec_ctx_finish(&exec_ctx);
}

void test_security_connector_already_in_arg(void) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.value.pointer.p = NULL;
  arg.key = GRPC_ARG_SECURITY_CONNECTOR;
  grpc_channel_args args;
  args.num_args = 1;
  args.args = &arg;
  grpc_channel *chan = grpc_secure_channel_create(NULL, NULL, &args, NULL);
  grpc_channel_element *elem =
      grpc_channel_stack_element(grpc_channel_get_channel_stack(chan), 0);
  GPR_ASSERT(0 == strcmp(elem->filter->name, "lame-client"));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_CHANNEL_INTERNAL_UNREF(&exec_ctx, chan, "test");
  grpc_exec_ctx_finish(&exec_ctx);
}

void test_null_creds(void) {
  grpc_channel *chan = grpc_secure_channel_create(NULL, NULL, NULL, NULL);
  grpc_channel_element *elem =
      grpc_channel_stack_element(grpc_channel_get_channel_stack(chan), 0);
  GPR_ASSERT(0 == strcmp(elem->filter->name, "lame-client"));
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_CHANNEL_INTERNAL_UNREF(&exec_ctx, chan, "test");
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_security_connector_already_in_arg();
  test_null_creds();
  test_unknown_scheme_target();
  grpc_shutdown();
  return 0;
}
