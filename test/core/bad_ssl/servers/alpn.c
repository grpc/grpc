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
#include <grpc/support/useful.h>

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "test/core/bad_ssl/server_common.h"
#include "test/core/end2end/data/ssl_test_data.h"

/* This test starts a server that is configured to advertise (via alpn and npn)
 * a protocol that the connecting client does not support. It does this by
 * overriding the functions declared in alpn.c from the core library. */

static const char *const fake_versions[] = {"not-h2"};

int grpc_chttp2_is_alpn_version_supported(const char *version, size_t size) {
  size_t i;
  for (i = 0; i < GPR_ARRAY_SIZE(fake_versions); i++) {
    if (!strncmp(version, fake_versions[i], size)) return 1;
  }
  return 0;
}

size_t grpc_chttp2_num_alpn_versions(void) {
  return GPR_ARRAY_SIZE(fake_versions);
}

const char *grpc_chttp2_get_alpn_version_index(size_t i) {
  GPR_ASSERT(i < GPR_ARRAY_SIZE(fake_versions));
  return fake_versions[i];
}

int main(int argc, char **argv) {
  const char *addr = bad_ssl_addr(argc, argv);
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {test_server1_key,
                                                  test_server1_cert};
  grpc_server_credentials *ssl_creds;
  grpc_server *server;

  grpc_init();
  ssl_creds =
      grpc_ssl_server_credentials_create(NULL, &pem_key_cert_pair, 1, 0, NULL);
  server = grpc_server_create(NULL, NULL);
  GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr, ssl_creds));
  grpc_server_credentials_release(ssl_creds);

  bad_ssl_run(server);
  grpc_shutdown();

  return 0;
}
