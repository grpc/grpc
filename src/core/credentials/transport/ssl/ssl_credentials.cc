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

#include "src/core/credentials/transport/ssl/ssl_credentials.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/credentials/transport/tls/ssl_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/tsi/ssl/session_cache/ssl_session_cache.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security_interface.h"

//
// SSL Channel Credentials.
//

grpc_ssl_credentials::grpc_ssl_credentials(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const grpc_ssl_verify_peer_options* verify_options) {
  build_config(pem_root_certs, pem_key_cert_pair, verify_options);
  // Use default (e.g. OS) root certificates if the user did not pass any root
  // certificates.
  if (config_.pem_root_certs == nullptr) {
    const char* pem_root_certs =
        grpc_core::DefaultSslRootStore::GetPemRootCerts();
    if (pem_root_certs == nullptr) {
      LOG(ERROR) << "Could not get default pem root certs.";
    } else {
      char* default_roots = gpr_strdup(pem_root_certs);
      config_.pem_root_certs = default_roots;
      root_store_ = grpc_core::DefaultSslRootStore::GetRootStore();
    }
  } else {
    config_.pem_root_certs = config_.pem_root_certs;
    root_store_ = nullptr;
  }

  client_handshaker_initialization_status_ = InitializeClientHandshakerFactory(
      &config_, config_.pem_root_certs, root_store_, nullptr,
      &client_handshaker_factory_);
}

grpc_ssl_credentials::~grpc_ssl_credentials() {
  gpr_free(config_.pem_root_certs);
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(config_.pem_key_cert_pair, 1);
  if (config_.verify_options.verify_peer_destruct != nullptr) {
    config_.verify_options.verify_peer_destruct(
        config_.verify_options.verify_peer_callback_userdata);
  }
  tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
}

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_ssl_credentials::create_security_connector(
    grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
    const char* target, grpc_core::ChannelArgs* args) {
  if (config_.pem_root_certs == nullptr) {
    LOG(ERROR) << "No root certs in config. Client-side security connector "
                  "must have root certs.";
    return nullptr;
  }
  std::optional<std::string> overridden_target_name =
      args->GetOwnedString(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
  auto* ssl_session_cache = args->GetObject<tsi::SslSessionLRUCache>();
  tsi_ssl_session_cache* session_cache =
      ssl_session_cache == nullptr ? nullptr : ssl_session_cache->c_ptr();

  grpc_core::RefCountedPtr<grpc_channel_security_connector> security_connector =
      nullptr;
  if (session_cache != nullptr) {
    // We need a separate factory and SSL_CTX if there's a cache in the channel
    // args. SSL_CTX should live with the factory and that should live on the
    // credentials. However, there is a way to configure a session cache in the
    // channel args, so that prevents us from also keeping the session cache at
    // the credentials level. In the case of a session cache, we still need to
    // keep a separate factory and SSL_CTX at the subchannel/security_connector
    // level.
    tsi_ssl_client_handshaker_factory* factory_with_cache = nullptr;
    grpc_security_status status = InitializeClientHandshakerFactory(
        &config_, config_.pem_root_certs, root_store_, session_cache,
        &factory_with_cache);
    if (status != GRPC_SECURITY_OK) {
      LOG(ERROR) << "InitializeClientHandshakerFactory returned bad status.";
      return nullptr;
    }
    security_connector = grpc_ssl_channel_security_connector_create(
        this->Ref(), std::move(call_creds), &config_, target,
        overridden_target_name.has_value() ? overridden_target_name->c_str()
                                           : nullptr,
        factory_with_cache);
    tsi_ssl_client_handshaker_factory_unref(factory_with_cache);
  } else {
    if (client_handshaker_initialization_status_ != GRPC_SECURITY_OK) {
      return nullptr;
    }
    security_connector = grpc_ssl_channel_security_connector_create(
        this->Ref(), std::move(call_creds), &config_, target,
        overridden_target_name.has_value() ? overridden_target_name->c_str()
                                           : nullptr,
        client_handshaker_factory_);
  }

  if (security_connector == nullptr) {
    return security_connector;
  }
  *args = args->Set(GRPC_ARG_HTTP2_SCHEME, "https");
  return security_connector;
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
    CHECK_NE(pem_key_cert_pair->private_key, nullptr);
    CHECK_NE(pem_key_cert_pair->cert_chain, nullptr);
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

grpc_security_status grpc_ssl_credentials::InitializeClientHandshakerFactory(
    const grpc_ssl_config* config, const char* pem_root_certs,
    const tsi_ssl_root_certs_store* root_store,
    tsi_ssl_session_cache* ssl_session_cache,
    tsi_ssl_client_handshaker_factory** handshaker_factory) {
  // This class level factory can't have a session cache by design. If we want
  // to init one with a cache we need to make a new one
  if (client_handshaker_factory_ != nullptr && ssl_session_cache == nullptr) {
    return GRPC_SECURITY_OK;
  }

  bool has_key_cert_pair = config->pem_key_cert_pair != nullptr &&
                           config->pem_key_cert_pair->private_key != nullptr &&
                           config->pem_key_cert_pair->cert_chain != nullptr;
  tsi_ssl_client_handshaker_options options;
  if (pem_root_certs == nullptr) {
    LOG(ERROR) << "Handshaker factory creation failed. pem_root_certs cannot "
                  "be nullptr";
    return GRPC_SECURITY_ERROR;
  }
  options.pem_root_certs = pem_root_certs;
  options.root_store = root_store;
  options.alpn_protocols =
      grpc_fill_alpn_protocol_strings(&options.num_alpn_protocols);
  if (has_key_cert_pair) {
    options.pem_key_cert_pair = config->pem_key_cert_pair;
  }
  options.cipher_suites = grpc_get_ssl_cipher_suites();
  options.session_cache = ssl_session_cache;
  options.min_tls_version = grpc_get_tsi_tls_version(config->min_tls_version);
  options.max_tls_version = grpc_get_tsi_tls_version(config->max_tls_version);
  const tsi_result result =
      tsi_create_ssl_client_handshaker_factory_with_options(&options,
                                                            handshaker_factory);
  gpr_free(options.alpn_protocols);
  if (result != TSI_OK) {
    LOG(ERROR) << "Handshaker factory creation failed with "
               << tsi_result_to_string(result);
    return GRPC_SECURITY_ERROR;
  }
  return GRPC_SECURITY_OK;
}

// Deprecated in favor of grpc_ssl_credentials_create_ex. Will be removed
// once all of its call sites are migrated to grpc_ssl_credentials_create_ex.
grpc_channel_credentials* grpc_ssl_credentials_create(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const verify_peer_options* verify_options, void* reserved) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_ssl_credentials_create(pem_root_certs=" << pem_root_certs
      << ", pem_key_cert_pair=" << pem_key_cert_pair
      << ", verify_options=" << verify_options << ", reserved=" << reserved
      << ")";
  CHECK_EQ(reserved, nullptr);

  return new grpc_ssl_credentials(
      pem_root_certs, pem_key_cert_pair,
      reinterpret_cast<const grpc_ssl_verify_peer_options*>(verify_options));
}

grpc_channel_credentials* grpc_ssl_credentials_create_ex(
    const char* pem_root_certs, grpc_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const grpc_ssl_verify_peer_options* verify_options, void* reserved) {
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_ssl_credentials_create(pem_root_certs=" << pem_root_certs
      << ", pem_key_cert_pair=" << pem_key_cert_pair
      << ", verify_options=" << verify_options << ", reserved=" << reserved
      << ")";
  CHECK_EQ(reserved, nullptr);

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
    CHECK_NE(pem_key_cert_pairs, nullptr);
    tsi_pairs = static_cast<tsi_ssl_pem_key_cert_pair*>(
        gpr_zalloc(num_key_cert_pairs * sizeof(tsi_ssl_pem_key_cert_pair)));
  }
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    CHECK_NE(pem_key_cert_pairs[i].private_key, nullptr);
    CHECK_NE(pem_key_cert_pairs[i].cert_chain, nullptr);
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
    CHECK_NE(pem_key_cert_pairs, nullptr);
    config->pem_key_cert_pairs = static_cast<grpc_ssl_pem_key_cert_pair*>(
        gpr_zalloc(num_key_cert_pairs * sizeof(grpc_ssl_pem_key_cert_pair)));
  }
  config->num_key_cert_pairs = num_key_cert_pairs;
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    CHECK_NE(pem_key_cert_pairs[i].private_key, nullptr);
    CHECK_NE(pem_key_cert_pairs[i].cert_chain, nullptr);
    config->pem_key_cert_pairs[i].cert_chain =
        gpr_strdup(pem_key_cert_pairs[i].cert_chain);
    config->pem_key_cert_pairs[i].private_key =
        gpr_strdup(pem_key_cert_pairs[i].private_key);
  }
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
    LOG(ERROR) << "Certificate config must not be NULL.";
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
    LOG(ERROR) << "Invalid certificate config callback parameter.";
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
  GRPC_TRACE_LOG(api, INFO)
      << "grpc_ssl_server_credentials_create_ex(pem_root_certs="
      << pem_root_certs << ", pem_key_cert_pairs=" << pem_key_cert_pairs
      << ", num_key_cert_pairs=" << (unsigned long)num_key_cert_pairs
      << ", client_certificate_request=" << client_certificate_request
      << ", reserved=" << reserved << ")";
  CHECK_EQ(reserved, nullptr);

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
    LOG(ERROR) << "Invalid options trying to create SSL server credentials.";
    goto done;
  }

  if (options->certificate_config == nullptr &&
      options->certificate_config_fetcher == nullptr) {
    LOG(ERROR) << "SSL server credentials options must specify either "
                  "certificate config or fetcher.";
    goto done;
  } else if (options->certificate_config_fetcher != nullptr &&
             options->certificate_config_fetcher->cb == nullptr) {
    LOG(ERROR) << "Certificate config fetcher callback must not be NULL.";
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
