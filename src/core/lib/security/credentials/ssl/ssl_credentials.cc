//
//
// Copyright 2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"

#include <string.h>

#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/tsi/ssl/session_cache/ssl_session_cache.h"
#include "src/core/tsi/ssl_transport_security.h"

//
// SSL Channel Credentials.
//

grpc_ssl_credentials::grpc_ssl_credentials(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const grpc_ssl_verify_peer_options* verify_options) {
  build_config(pem_root_certs, pem_key_cert_pair, verify_options);
}

grpc_ssl_credentials::~grpc_ssl_credentials() {
  gpr_free(config_.pem_root_certs);
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(config_.pem_key_cert_pair, 1);
  if (config_.verify_options.verify_peer_destruct != nullptr) {
    config_.verify_options.verify_peer_destruct(
        config_.verify_options.verify_peer_callback_userdata);
  }
}

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_ssl_credentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target, grpc_core::ChannelArgs* args) {
  absl::optional<std::string> overridden_target_name =
      args->GetOwnedString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
  auto* ssl_session_cache = args->GetObject<tsi::SslSessionLRUCache>();
  grpc_core::RefCountedPtr<grpc_channel_security_connector> sc =
      grpc_ssl_channel_security_connector_create(
          this->Ref(), std::move(call_creds), &config_, target,
          overridden_target_name.has_value() ? overridden_target_name->c_str()
                                             : nullptr,
          ssl_session_cache == nullptr ? nullptr : ssl_session_cache->c_ptr());
  if (sc == nullptr) {
    return sc;
  }
  *args = args->Set(GRPC_ARG_HTTP2_SCHEME, "https");
  return sc;
}

grpc_core::UniqueTypeName grpc_ssl_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Ssl");
  return kFactory.Create();
}

void grpc_ssl_credentials::build_config(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const grpc_ssl_verify_peer_options* verify_options) {
  config_.pem_root_certs = gpr_strdup(pem_root_certs);
  if (pem_key_cert_pair != nullptr) {
    GPR_ASSERT(pem_key_cert_pair->private_key != nullptr);
    GPR_ASSERT(pem_key_cert_pair->cert_chain != nullptr);
    config_.pem_key_cert_pair = static_cast<tsi_ssl_pem_key_cert_pair*>(
        gpr_zalloc(sizeof(tsi_ssl_pem_key_cert_pair)));
    config_.pem_key_cert_pair->cert_chain =
        gpr_strdup(pem_key_cert_pair->cert_chain);
    config_.pem_key_cert_pair->private_key =
        gpr_strdup(pem_key_cert_pair->private_key);
  } else {
    config_.pem_key_cert_pair = nullptr;
  }
  if (verify_options != nullptr) {
    memcpy(&config_.verify_options, verify_options,
           sizeof(verify_peer_options));
  } else {
    // Otherwise set all options to default values
    memset(&config_.verify_options, 0, sizeof(verify_peer_options));
  }
}

void grpc_ssl_credentials::set_min_tls_version(
    grpc_tls_version min_tls_version) {
  config_.min_tls_version = min_tls_version;
}

void grpc_ssl_credentials::set_max_tls_version(
    grpc_tls_version max_tls_version) {
  config_.max_tls_version = max_tls_version;
}

// Deprecated in favor of grpc_ssl_credentials_create_ex. Will be removed
// once all of its call sites are migrated to grpc_ssl_credentials_create_ex.
grpc_channel_credentials* grpc_ssl_credentials_create(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const verify_peer_options* verify_options, void* reserved) {
  GRPC_API_TRACE(
      "grpc_ssl_credentials_create(pem_root_certs=%s, "
      "pem_key_cert_pair=%p, "
      "verify_options=%p, "
      "reserved=%p)",
      4, (pem_root_certs, pem_key_cert_pair, verify_options, reserved));
  GPR_ASSERT(reserved == nullptr);

  return new grpc_ssl_credentials(
      pem_root_certs, pem_key_cert_pair,
      reinterpret_cast<const grpc_ssl_verify_peer_options*>(verify_options));
}

grpc_channel_credentials* grpc_ssl_credentials_create_ex(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const grpc_ssl_verify_peer_options* verify_options, void* reserved) {
  GRPC_API_TRACE(
      "grpc_ssl_credentials_create(pem_root_certs=%s, "
      "pem_key_cert_pair=%p, "
      "verify_options=%p, "
      "reserved=%p)",
      4, (pem_root_certs, pem_key_cert_pair, verify_options, reserved));
  GPR_ASSERT(reserved == nullptr);

  return new grpc_ssl_credentials(pem_root_certs, pem_key_cert_pair,
                                  verify_options);
}

//
// SSL Server Credentials.
//

struct grpc_ssl_server_credentials_options {
  grpc_ssl_client_certificate_request_type client_certificate_request;
  grpc_ssl_server_certificate_config* certificate_config;
  grpc_ssl_server_certificate_config_fetcher* certificate_config_fetcher;
};

grpc_ssl_server_credentials::grpc_ssl_server_credentials(
    const grpc_ssl_server_credentials_options& options) {
  if (options.certificate_config_fetcher != nullptr) {
    config_.client_certificate_request = options.client_certificate_request;
    certificate_config_fetcher_ = *options.certificate_config_fetcher;
  } else {
    build_config(options.certificate_config->pem_root_certs,
                 options.certificate_config->pem_key_cert_pairs,
                 options.certificate_config->num_key_cert_pairs,
                 options.client_certificate_request);
  }
}

grpc_ssl_server_credentials::~grpc_ssl_server_credentials() {
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(config_.pem_key_cert_pairs,
                                          config_.num_key_cert_pairs);
  gpr_free(config_.pem_root_certs);
}
grpc_core::RefCountedPtr<grpc_server_security_connector>
grpc_ssl_server_credentials::create_security_connector(
    const grpc_core::ChannelArgs& /* args */) {
  return grpc_ssl_server_security_connector_create(this->Ref());
}

grpc_core::UniqueTypeName grpc_ssl_server_credentials::Type() {
  static grpc_core::UniqueTypeName::Factory kFactory("Ssl");
  return kFactory.Create();
}

tsi_ssl_pem_key_cert_pair* grpc_convert_grpc_to_tsi_cert_pairs(
    const grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs) {
  tsi_ssl_pem_key_cert_pair* tsi_pairs = nullptr;
  if (num_key_cert_pairs > 0) {
    GPR_ASSERT(pem_key_cert_pairs != nullptr);
    tsi_pairs = static_cast<tsi_ssl_pem_key_cert_pair*>(
        gpr_zalloc(num_key_cert_pairs * sizeof(tsi_ssl_pem_key_cert_pair)));
  }
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    GPR_ASSERT(pem_key_cert_pairs[i].private_key != nullptr);
    GPR_ASSERT(pem_key_cert_pairs[i].cert_chain != nullptr);
    tsi_pairs[i].cert_chain = gpr_strdup(pem_key_cert_pairs[i].cert_chain);
    tsi_pairs[i].private_key = gpr_strdup(pem_key_cert_pairs[i].private_key);
  }
  return tsi_pairs;
}

void grpc_ssl_server_credentials::build_config(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_certificate_request) {
  config_.client_certificate_request = client_certificate_request;
  config_.pem_root_certs = gpr_strdup(pem_root_certs);
  config_.pem_key_cert_pairs = grpc_convert_grpc_to_tsi_cert_pairs(
      pem_key_cert_pairs, num_key_cert_pairs);
  config_.num_key_cert_pairs = num_key_cert_pairs;
  config_.set_client_ca_list = set_client_ca_list_;
}

void grpc_ssl_server_credentials::set_min_tls_version(
    grpc_tls_version min_tls_version) {
  config_.min_tls_version = min_tls_version;
}

void grpc_ssl_server_credentials::set_max_tls_version(
    grpc_tls_version max_tls_version) {
  config_.max_tls_version = max_tls_version;
}

grpc_ssl_server_certificate_config* grpc_ssl_server_certificate_config_create(
    const char* pem_root_certs,
    const grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs) {
  grpc_ssl_server_certificate_config* config =
      static_cast<grpc_ssl_server_certificate_config*>(
          gpr_zalloc(sizeof(grpc_ssl_server_certificate_config)));
  config->pem_root_certs = gpr_strdup(pem_root_certs);
  if (num_key_cert_pairs > 0) {
    GPR_ASSERT(pem_key_cert_pairs != nullptr);
    config->pem_key_cert_pairs = static_cast<grpc_ssl_pem_key_cert_pair*>(
        gpr_zalloc(num_key_cert_pairs * sizeof(grpc_ssl_pem_key_cert_pair)));
  }
  config->num_key_cert_pairs = num_key_cert_pairs;
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    GPR_ASSERT(pem_key_cert_pairs[i].private_key != nullptr);
    GPR_ASSERT(pem_key_cert_pairs[i].cert_chain != nullptr);
    config->pem_key_cert_pairs[i].cert_chain =
        gpr_strdup(pem_key_cert_pairs[i].cert_chain);
    config->pem_key_cert_pairs[i].private_key =
        gpr_strdup(pem_key_cert_pairs[i].private_key);
  }
  config->set_client_ca_list = set_client_ca_list_;
  return config;
}

void grpc_ssl_server_certificate_config_destroy(
    grpc_ssl_server_certificate_config* config) {
  if (config == nullptr) return;
  for (size_t i = 0; i < config->num_key_cert_pairs; i++) {
    gpr_free(const_cast<char*>(config->pem_key_cert_pairs[i].private_key));
    gpr_free(const_cast<char*>(config->pem_key_cert_pairs[i].cert_chain));
  }
  gpr_free(config->pem_key_cert_pairs);
  gpr_free(config->pem_root_certs);
  gpr_free(config);
}

grpc_ssl_server_credentials_options*
grpc_ssl_server_credentials_create_options_using_config(
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_certificate_config* config) {
  grpc_ssl_server_credentials_options* options = nullptr;
  if (config == nullptr) {
    gpr_log(GPR_ERROR, "Certificate config must not be NULL.");
    goto done;
  }
  options = static_cast<grpc_ssl_server_credentials_options*>(
      gpr_zalloc(sizeof(grpc_ssl_server_credentials_options)));
  options->client_certificate_request = client_certificate_request;
  options->certificate_config = config;
done:
  return options;
}

grpc_ssl_server_credentials_options*
grpc_ssl_server_credentials_create_options_using_config_fetcher(
    grpc_ssl_client_certificate_request_type client_certificate_request,
    grpc_ssl_server_certificate_config_callback cb, void* user_data) {
  if (cb == nullptr) {
    gpr_log(GPR_ERROR, "Invalid certificate config callback parameter.");
    return nullptr;
  }

  grpc_ssl_server_certificate_config_fetcher* fetcher =
      static_cast<grpc_ssl_server_certificate_config_fetcher*>(
          gpr_zalloc(sizeof(grpc_ssl_server_certificate_config_fetcher)));
  fetcher->cb = cb;
  fetcher->user_data = user_data;

  grpc_ssl_server_credentials_options* options =
      static_cast<grpc_ssl_server_credentials_options*>(
          gpr_zalloc(sizeof(grpc_ssl_server_credentials_options)));
  options->client_certificate_request = client_certificate_request;
  options->certificate_config_fetcher = fetcher;

  return options;
}

grpc_server_credentials* grpc_ssl_server_credentials_create(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs, int force_client_auth, void* reserved) {
  return grpc_ssl_server_credentials_create_ex(
      pem_root_certs, pem_key_cert_pairs, num_key_cert_pairs,
      force_client_auth
          ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
          : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
      reserved);
}

grpc_server_credentials* grpc_ssl_server_credentials_create_ex(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs,
    grpc_ssl_client_certificate_request_type client_certificate_request,
    void* reserved) {
  GRPC_API_TRACE(
      "grpc_ssl_server_credentials_create_ex("
      "pem_root_certs=%s, pem_key_cert_pairs=%p, num_key_cert_pairs=%lu, "
      "client_certificate_request=%d, reserved=%p)",
      5,
      (pem_root_certs, pem_key_cert_pairs, (unsigned long)num_key_cert_pairs,
       client_certificate_request, reserved));
  GPR_ASSERT(reserved == nullptr);

  grpc_ssl_server_certificate_config* cert_config =
      grpc_ssl_server_certificate_config_create(
          pem_root_certs, pem_key_cert_pairs, num_key_cert_pairs);
  grpc_ssl_server_credentials_options* options =
      grpc_ssl_server_credentials_create_options_using_config(
          client_certificate_request, cert_config);

  return grpc_ssl_server_credentials_create_with_options(options);
}

grpc_server_credentials* grpc_ssl_server_credentials_create_with_options(
    grpc_ssl_server_credentials_options* options) {
  grpc_server_credentials* retval = nullptr;

  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "Invalid options trying to create SSL server credentials.");
    goto done;
  }

  if (options->certificate_config == nullptr &&
      options->certificate_config_fetcher == nullptr) {
    gpr_log(GPR_ERROR,
            "SSL server credentials options must specify either "
            "certificate config or fetcher.");
    goto done;
  } else if (options->certificate_config_fetcher != nullptr &&
             options->certificate_config_fetcher->cb == nullptr) {
    gpr_log(GPR_ERROR, "Certificate config fetcher callback must not be NULL.");
    goto done;
  }

  retval = new grpc_ssl_server_credentials(*options);

done:
  grpc_ssl_server_credentials_options_destroy(options);
  return retval;
}

void grpc_ssl_server_credentials_options_destroy(
    grpc_ssl_server_credentials_options* o) {
  if (o == nullptr) return;
  gpr_free(o->certificate_config_fetcher);
  grpc_ssl_server_certificate_config_destroy(o->certificate_config);
  gpr_free(o);
}
