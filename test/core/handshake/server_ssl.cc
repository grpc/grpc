/*
 *
 * Copyright 2016 gRPC authors.
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

#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

#include "test/core/handshake/server_ssl_common.h"

int main(int argc, char* argv[]) {
  // Handshake succeeeds when the client supplies the standard ALPN list.
  const char* full_alpn_list[] = {"grpc-exp", "h2"};
  GPR_ASSERT(server_ssl_test(full_alpn_list, 2, "grpc-exp"));
  // Handshake succeeeds when the client supplies only h2 as the ALPN list. This
  // covers legacy gRPC clients which don't support grpc-exp.
  const char* h2_only_alpn_list[] = {"h2"};
  GPR_ASSERT(server_ssl_test(h2_only_alpn_list, 1, "h2"));
  // Handshake succeeds when the client supplies superfluous ALPN entries and
  // also when h2 precedes gprc-exp.
  const char* extra_alpn_list[] = {"foo", "h2", "bar", "grpc-exp"};
  GPR_ASSERT(server_ssl_test(extra_alpn_list, 4, "h2"));
  // Handshake fails when the client uses a fake protocol as its only ALPN
  // preference. This validates the server is correctly validating ALPN
  // and sanity checks the server_ssl_test.
  const char* fake_alpn_list[] = {"foo"};
  GPR_ASSERT(!server_ssl_test(fake_alpn_list, 1, "foo"));
  return 0;
}
