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

#include "src/core/ext/transport/chttp2/alpn/alpn.h"

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/log.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/bad_ssl/server_common.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define SERVER_CERT_PATH "src/core/tsi/test_creds/server1.pem"
#define SERVER_KEY_PATH "src/core/tsi/test_creds/server1.key"

// This test starts a server that is configured to advertise (via alpn and npn)
// a protocol that the connecting client does not support. It does this by
// overriding the functions declared in alpn.c from the core library.

static const char* const fake_versions[] = {"not-h2"};

int grpc_chttp2_is_alpn_version_supported(const char* version, size_t size) {
  size_t i;
  for (i = 0; i < GPR_ARRAY_SIZE(fake_versions); i++) {
    if (!strncmp(version, fake_versions[i], size)) return 1;
  }
  return 0;
}

size_t grpc_chttp2_num_alpn_versions(void) {
  return GPR_ARRAY_SIZE(fake_versions);
}

const char* grpc_chttp2_get_alpn_version_index(size_t i) {
  GPR_ASSERT(i < GPR_ARRAY_SIZE(fake_versions));
  return fake_versions[i];
}

int main(int argc, char** argv) {
  const char* addr = bad_ssl_addr(argc, argv);
  grpc_slice cert_slice, key_slice;
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "load_file", grpc_load_file(SERVER_CERT_PATH, 1, &cert_slice)));
  GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(SERVER_KEY_PATH, 1, &key_slice)));
  const char* server_cert =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(cert_slice);
  const char* server_key =
      reinterpret_cast<const char*> GRPC_SLICE_START_PTR(key_slice);
  grpc_ssl_pem_key_cert_pair pem_key_cert_pair = {server_key, server_cert};
  grpc_server_credentials* ssl_creds;
  grpc_server* server;

  grpc_init();
  ssl_creds = grpc_ssl_server_credentials_create(nullptr, &pem_key_cert_pair, 1,
                                                 0, nullptr);
  server = grpc_server_create(nullptr, nullptr);
  GPR_ASSERT(grpc_server_add_http2_port(server, addr, ssl_creds));
  grpc_server_credentials_release(ssl_creds);

  bad_ssl_run(server);
  grpc_slice_unref(cert_slice);
  grpc_slice_unref(key_slice);
  grpc_shutdown();

  return 0;
}
