//
//
// Copyright 2024 gRPC authors.
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

#include "src/core/handshaker/http_connect/http_proxy_tls_credentials.h"

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <optional>
#include <string>

#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/log/log.h"

namespace grpc_core {

RefCountedPtr<grpc_channel_credentials> CreateHttpProxyTlsCredentials(
    const ChannelArgs& args) {
  // Get root certificates (optional - use system defaults if not specified)
  std::optional<std::string> root_certs =
      args.GetOwnedString(GRPC_ARG_HTTP_PROXY_TLS_ROOT_CERTS);

  // Get client certificate and key for mTLS (optional)
  std::optional<std::string> cert_chain =
      args.GetOwnedString(GRPC_ARG_HTTP_PROXY_TLS_CERT_CHAIN);
  std::optional<std::string> private_key =
      args.GetOwnedString(GRPC_ARG_HTTP_PROXY_TLS_PRIVATE_KEY);

  // Get verification settings
  bool verify_server_cert =
      args.GetBool(GRPC_ARG_HTTP_PROXY_TLS_VERIFY_SERVER_CERT).value_or(true);

  // Create TLS credentials options
  grpc_tls_credentials_options* options = grpc_tls_credentials_options_create();
  if (options == nullptr) {
    LOG(ERROR) << "Failed to create TLS credentials options for HTTPS proxy";
    return nullptr;
  }

  // Configure certificate provider if we have root certs or client identity
  if (root_certs.has_value() ||
      (cert_chain.has_value() && private_key.has_value())) {
    grpc_tls_identity_pairs* identity_pairs = nullptr;

    // Set up client identity for mTLS if both cert and key are provided
    if (cert_chain.has_value() && private_key.has_value()) {
      identity_pairs = grpc_tls_identity_pairs_create();
      if (identity_pairs != nullptr) {
        grpc_tls_identity_pairs_add_pair(identity_pairs, private_key->c_str(),
                                         cert_chain->c_str());
      }
    }

    // Create certificate provider with root certs and/or identity
    grpc_tls_certificate_provider* provider =
        grpc_tls_certificate_provider_static_data_create(
            root_certs.has_value() ? root_certs->c_str() : nullptr,
            identity_pairs);

    if (provider != nullptr) {
      grpc_tls_credentials_options_set_certificate_provider(options, provider);

      // Watch root certs if provided
      if (root_certs.has_value()) {
        grpc_tls_credentials_options_watch_root_certs(options);
      }

      // Watch identity key cert pairs if client identity provided
      if (identity_pairs != nullptr) {
        grpc_tls_credentials_options_watch_identity_key_cert_pairs(options);
      }

      grpc_tls_certificate_provider_release(provider);
    }

    if (identity_pairs != nullptr) {
      grpc_tls_identity_pairs_destroy(identity_pairs);
    }
  }

  // Configure server certificate verification
  grpc_tls_credentials_options_set_verify_server_cert(options,
                                                      verify_server_cert);

  // Create TLS channel credentials
  grpc_channel_credentials* creds = grpc_tls_credentials_create(options);
  if (creds == nullptr) {
    LOG(ERROR) << "Failed to create TLS credentials for HTTPS proxy";
    return nullptr;
  }

  return RefCountedPtr<grpc_channel_credentials>(creds);
}

}  // namespace grpc_core
