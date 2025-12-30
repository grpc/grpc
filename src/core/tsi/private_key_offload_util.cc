//
//
// Copyright 2025 gRPC authors.
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

#include "src/core/tsi/private_key_offload_util.h"

#include <openssl/ssl.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "src/core/tsi/transport_security_interface.h"
#include "absl/functional/bind_front.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

#if defined(OPENSSL_IS_BORINGSSL)

enum ssl_private_key_result_t TlsPrivateKeyOffloadComplete(SSL* ssl,
                                                           uint8_t* out,
                                                           size_t* out_len,
                                                           size_t max_out) {
  TlsPrivateKeyOffloadContext* ctx = GetTlsPrivateKeyOffloadContext(ssl);
  if (!ctx->signed_bytes.ok()) {
    return ssl_private_key_failure;
  }
  // Important bit is moving the signed data where it needs to go
  const std::string& signed_data = *ctx->signed_bytes;
  if (signed_data.length() > max_out) {
    // Result is too large.
    return ssl_private_key_failure;
  }
  memcpy(out, signed_data.data(), signed_data.length());
  *out_len = signed_data.length();
  // Tell BoringSSL we're done
  return ssl_private_key_success;
}
#endif  // OPENSSL_IS_BORINGSSL

}  // namespace grpc_core
