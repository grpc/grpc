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

#include "src/core/lib/iomgr/load_file.h"

#include "test/core/bad_ssl/server_common.h"
#include "test/core/end2end/data/ssl_test_data.h"

/* This server will present an untrusted cert to the connecting client,
 * causing the SSL handshake to fail */

int main(int argc, char **argv) {
  const char *addr = bad_ssl_addr(argc, argv);
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
  grpc_server_credentials *ssl_creds;
  grpc_server *server;
  grpc_slice cert_slice, key_slice;

  grpc_init();

  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file",
      grpc_load_file("src/core/tsi/test_creds/badserver.pem", 1, &cert_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file",
      grpc_load_file("src/core/tsi/test_creds/badserver.key", 1, &key_slice)));
  pem_key_cert_pair.private_key = (const char *)GRPC_SLICE_START_PTR(key_slice);
  pem_key_cert_pair.cert_chain = (const char *)GRPC_SLICE_START_PTR(cert_slice);

  ssl_creds =
      grpc_ssl_server_credentials_create(NULL, &pem_key_cert_pair, 1, 0, NULL);
  server = grpc_server_create(NULL, NULL);
  GPR_ASSERT(grpc_server_add_secure_http2_port(server, addr, ssl_creds));
  grpc_server_credentials_release(ssl_creds);

  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);

  bad_ssl_run(server);
  grpc_shutdown();

  return 0;
}
