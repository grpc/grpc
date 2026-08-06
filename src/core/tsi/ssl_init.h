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

#ifndef GRPC_SRC_CORE_TSI_SSL_INIT_H
#define GRPC_SRC_CORE_TSI_SSL_INIT_H

#include <grpc/support/port_platform.h>

namespace tsi {

// Initializes the OpenSSL library for gRPC, once per process.
//
// Call before the first OpenSSL API use on any code path that can run before a
// handshaker factory is created: parsing a key or a CRL while a credentials
// object is being constructed, or loading the default root store. Those calls
// otherwise trigger OpenSSL's implicit lazy initialization, which resolves the
// same register-atexit run-once that OPENSSL_INIT_NO_ATEXIT resolves and
// permanently installs OPENSSL_cleanup. Whichever reaches that run-once first
// decides.
//
// OPENSSL_cleanup frees OpenSSL's global locks and thread-local state at
// process exit, which is unsafe while EventEngine threads are still running.
//
// This lives in its own library rather than in ssl_transport_security.h
// because the credentials code that needs it is a dependency of the SSL
// transport security library, not the other way around.
void InitOpenSslOnce();

}  // namespace tsi

#endif  // GRPC_SRC_CORE_TSI_SSL_INIT_H
