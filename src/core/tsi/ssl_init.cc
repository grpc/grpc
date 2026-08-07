//
//
// Copyright 2026 gRPC authors.
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

#include "src/core/tsi/ssl_init.h"

#include <grpc/support/sync.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>

namespace tsi {
namespace {

gpr_once g_init_openssl_once = GPR_ONCE_INIT;

void InitOpenSsl() {
#if OPENSSL_VERSION_NUMBER >= 0x10101000L
  OPENSSL_init_ssl(OPENSSL_INIT_NO_ATEXIT, nullptr);
#else
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
#endif
}

}  // namespace

void InitOpenSslOnce() { gpr_once_init(&g_init_openssl_once, InitOpenSsl); }

}  // namespace tsi
