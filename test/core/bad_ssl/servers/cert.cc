//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/bad_ssl/server_common.h"

// This server will present an untrusted cert to the connecting client,
// causing the SSL handshake to fail

int main(int argc, char** argv) {
  const char* addr = bad_ssl_addr(argc, argv);
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair;
  grpc_server_credentials* ssl_creds;
  grpc_server* server;
  grpc_slice cert_slice, key_slice;

  grpc_init();

  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file",
      grpc_load_file("src/core/tsi/test_creds/badserver.pem", 1, &cert_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file",
      grpc_load_file("src/core/tsi/test_creds/badserver.key", 1, &key_slice)));
  pem_key_cert_pair.private_key =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
  pem_key_cert_pair.cert_chain =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);

  ssl_creds = grpc_ssl_server_credentials_create(nullptr, &pem_key_cert_pair, 1,
                                                 0, nullptr);
  server = grpc_server_create(nullptr, nullptr);
  GPR_ASSERT(grpc_server_add_http2_port(server, addr, ssl_creds));
  grpc_server_credentials_release(ssl_creds);

  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);

  bad_ssl_run(server);
  grpc_shutdown();

  return 0;
}
