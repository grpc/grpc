//
//
// Copyright 2018 gRPC authors.
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

#include "src/core/lib/security/security_connector/ssl/ssl_security_connector.h"

#include <stdint.h>
#include <string.h>

#include <initializer_list>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/transport/handshaker.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"
#include "src/core/tsi/transport_security_interface.h"

namespace {
grpc_error_handle ssl_check_peer(
    const char* peer_name, const tsi_peer* peer,
    grpc_core::RefCountedPtr<grpc_auth_context>* auth_context) {
  grpc_error_handle error = grpc_ssl_check_alpn(peer);
  if (!error.ok()) {
    return error;
  }
  // Check the peer name if specified.
  if (peer_name != nullptr && !grpc_ssl_host_matches_name(peer, peer_name)) {
    return GRPC_ERROR_CREATE(
        absl::StrCat("Peer name ", peer_name, " is not in peer certificate"));
  }
  *auth_context =
      grpc_ssl_peer_to_auth_context(peer, GRPC_SSL_TRANSPORT_SECURITY_TYPE);
  return absl::OkStatus();
}

class grpc_ssl_channel_security_connector final
    : public grpc_channel_security_connector {
 public:
  grpc_ssl_channel_security_connector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const grpc_ssl_config* config, const char* target_name,
      const char* overridden_target_name)
      : grpc_channel_security_connector(GRPC_SSL_URL_SCHEME,
                                        std::move(channel_creds),
                                        std::move(request_metadata_creds)),
        overridden_target_name_(
            overridden_target_name == nullptr ? "" : overridden_target_name),
        verify_options_(&config->verify_options) {
    absl::string_view host;
    absl::string_view port;
    grpc_core::SplitHostPort(target_name, &host, &port);
    target_name_ = std::string(host);
  }

  ~grpc_ssl_channel_security_connector() override {
    tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
  }

  grpc_security_status InitializeHandshakerFactory(
      const grpc_ssl_config* config, const char* pem_root_certs,
      const tsi_ssl_root_certs_store* root_store,
      tsi_ssl_session_cache* ssl_session_cache) {
    bool has_key_cert_pair =
        config->pem_key_cert_pair != nullptr &&
        config->pem_key_cert_pair->private_key != nullptr &&
        config->pem_key_cert_pair->cert_chain != nullptr;
    tsi_ssl_client_handshaker_options options;
    GPR_DEBUG_ASSERT(pem_root_certs != nullptr);
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
        tsi_create_ssl_client_handshaker_factory_with_options(
            &options, &client_handshaker_factory_);
    gpr_free(options.alpn_protocols);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
              tsi_result_to_string(result));
      return GRPC_SECURITY_ERROR;
    }
    return GRPC_SECURITY_OK;
  }

  void add_handshakers(const grpc_core::ChannelArgs& args,
                       grpc_pollset_set* /*interested_parties*/,
                       grpc_core::HandshakeManager* handshake_mgr) override {
    // Instantiate TSI handshaker.
    tsi_handshaker* tsi_hs = nullptr;
    tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
        client_handshaker_factory_,
        overridden_target_name_.empty() ? target_name_.c_str()
                                        : overridden_target_name_.c_str(),
        /*network_bio_buf_size=*/0,
        /*ssl_bio_buf_size=*/0, &tsi_hs);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
              tsi_result_to_string(result));
      return;
    }
    // Create handshakers.
    handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(tsi_hs, this, args));
  }

  void check_peer(tsi_peer peer, grpc_endpoint* /*ep*/,
                  const grpc_core::ChannelArgs& /*args*/,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    const char* target_name = overridden_target_name_.empty()
                                  ? target_name_.c_str()
                                  : overridden_target_name_.c_str();
    grpc_error_handle error = ssl_check_peer(target_name, &peer, auth_context);
    if (error.ok() && verify_options_->verify_peer_callback != nullptr) {
      const tsi_peer_property* p =
          tsi_peer_get_property_by_name(&peer, TSI_X509_PEM_CERT_PROPERTY);
      if (p == nullptr) {
        error =
            GRPC_ERROR_CREATE("Cannot check peer: missing pem cert property.");
      } else {
        char* peer_pem = static_cast<char*>(gpr_malloc(p->value.length + 1));
        memcpy(peer_pem, p->value.data, p->value.length);
        peer_pem[p->value.length] = '\0';
        int callback_status = verify_options_->verify_peer_callback(
            target_name, peer_pem,
            verify_options_->verify_peer_callback_userdata);
        gpr_free(peer_pem);
        if (callback_status) {
          error = GRPC_ERROR_CREATE(absl::StrFormat(
              "Verify peer callback returned a failure (%d)", callback_status));
        }
      }
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
    tsi_peer_destruct(&peer);
  }

  void cancel_check_peer(grpc_closure* /*on_peer_checked*/,
                         grpc_error_handle /*error*/) override {}

  int cmp(const grpc_security_connector* other_sc) const override {
    auto* other =
        reinterpret_cast<const grpc_ssl_channel_security_connector*>(other_sc);
    int c = channel_security_connector_cmp(other);
    if (c != 0) return c;
    c = target_name_.compare(other->target_name_);
    if (c != 0) return c;
    return overridden_target_name_.compare(other->overridden_target_name_);
  }

  grpc_core::ArenaPromise<absl::Status> CheckCallHost(
      absl::string_view host, grpc_auth_context* auth_context) override {
    return grpc_core::Immediate(
        SslCheckCallHost(host, target_name_.c_str(),
                         overridden_target_name_.c_str(), auth_context));
  }

 private:
  tsi_ssl_client_handshaker_factory* client_handshaker_factory_;
  std::string target_name_;
  std::string overridden_target_name_;
  const verify_peer_options* verify_options_;
};

class grpc_ssl_server_security_connector
    : public grpc_server_security_connector {
 public:
  explicit grpc_ssl_server_security_connector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds)
      : grpc_server_security_connector(GRPC_SSL_URL_SCHEME,
                                       std::move(server_creds)) {}

  ~grpc_ssl_server_security_connector() override {
    tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
  }

  bool has_cert_config_fetcher() const {
    return static_cast<const grpc_ssl_server_credentials*>(server_creds())
        ->has_cert_config_fetcher();
  }

  const tsi_ssl_server_handshaker_factory* server_handshaker_factory() const {
    return server_handshaker_factory_;
  }

  grpc_security_status InitializeHandshakerFactory() {
    if (has_cert_config_fetcher()) {
      // Load initial credentials from certificate_config_fetcher:
      if (!try_fetch_ssl_server_credentials()) {
        gpr_log(GPR_ERROR,
                "Failed loading SSL server credentials from fetcher.");
        return GRPC_SECURITY_ERROR;
      }
    } else {
      auto* server_credentials =
          static_cast<const grpc_ssl_server_credentials*>(server_creds());
      size_t num_alpn_protocols = 0;
      const char** alpn_protocol_strings =
          grpc_fill_alpn_protocol_strings(&num_alpn_protocols);
      tsi_ssl_server_handshaker_options options;
      options.pem_key_cert_pairs =
          server_credentials->config().pem_key_cert_pairs;
      options.num_key_cert_pairs =
          server_credentials->config().num_key_cert_pairs;
      options.pem_client_root_certs =
          server_credentials->config().pem_root_certs;
      options.client_certificate_request =
          grpc_get_tsi_client_certificate_request_type(
              server_credentials->config().client_certificate_request);
      options.cipher_suites = grpc_get_ssl_cipher_suites();
      options.alpn_protocols = alpn_protocol_strings;
      options.num_alpn_protocols = static_cast<uint16_t>(num_alpn_protocols);
      options.min_tls_version = grpc_get_tsi_tls_version(
          server_credentials->config().min_tls_version);
      options.max_tls_version = grpc_get_tsi_tls_version(
          server_credentials->config().max_tls_version);
      options.send_client_ca_list =
          server_credentials->config().send_client_ca_list;
      const tsi_result result =
          tsi_create_ssl_server_handshaker_factory_with_options(
              &options, &server_handshaker_factory_);
      gpr_free(alpn_protocol_strings);
      if (result != TSI_OK) {
        gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
                tsi_result_to_string(result));
        return GRPC_SECURITY_ERROR;
      }
    }
    return GRPC_SECURITY_OK;
  }

  void add_handshakers(const grpc_core::ChannelArgs& args,
                       grpc_pollset_set* /*interested_parties*/,
                       grpc_core::HandshakeManager* handshake_mgr) override {
    // Instantiate TSI handshaker.
    try_fetch_ssl_server_credentials();
    tsi_handshaker* tsi_hs = nullptr;
    tsi_result result = tsi_ssl_server_handshaker_factory_create_handshaker(
        server_handshaker_factory_, /*network_bio_buf_size=*/0,
        /*ssl_bio_buf_size=*/0, &tsi_hs);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
              tsi_result_to_string(result));
      return;
    }
    // Create handshakers.
    handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(tsi_hs, this, args));
  }

  void check_peer(tsi_peer peer, grpc_endpoint* /*ep*/,
                  const grpc_core::ChannelArgs& /*args*/,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    grpc_error_handle error = ssl_check_peer(nullptr, &peer, auth_context);
    tsi_peer_destruct(&peer);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
  }

  void cancel_check_peer(grpc_closure* /*on_peer_checked*/,
                         grpc_error_handle /*error*/) override {}

  int cmp(const grpc_security_connector* other) const override {
    return server_security_connector_cmp(
        static_cast<const grpc_server_security_connector*>(other));
  }

 private:
  // Attempts to fetch the server certificate config if a callback is available.
  // Current certificate config will continue to be used if the callback returns
  // an error. Returns true if new credentials were successfully loaded.
  bool try_fetch_ssl_server_credentials() {
    grpc_ssl_server_certificate_config* certificate_config = nullptr;
    bool status;
    if (!has_cert_config_fetcher()) return false;

    grpc_core::MutexLock lock(&mu_);
    grpc_ssl_server_credentials* server_creds =
        static_cast<grpc_ssl_server_credentials*>(this->mutable_server_creds());
    grpc_ssl_certificate_config_reload_status cb_result =
        server_creds->FetchCertConfig(&certificate_config);
    if (cb_result == GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED) {
      gpr_log(GPR_DEBUG, "No change in SSL server credentials.");
      status = false;
    } else if (cb_result == GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW) {
      status = try_replace_server_handshaker_factory(certificate_config);
    } else {
      // Log error, continue using previously-loaded credentials.
      gpr_log(GPR_ERROR,
              "Failed fetching new server credentials, continuing to "
              "use previously-loaded credentials.");
      status = false;
    }

    if (certificate_config != nullptr) {
      grpc_ssl_server_certificate_config_destroy(certificate_config);
    }
    return status;
  }

  // Attempts to replace the server_handshaker_factory with a new factory using
  // the provided grpc_ssl_server_certificate_config. Should new factory
  // creation fail, the existing factory will not be replaced. Returns true on
  // success (new factory created).
  bool try_replace_server_handshaker_factory(
      const grpc_ssl_server_certificate_config* config) {
    if (config == nullptr) {
      gpr_log(GPR_ERROR,
              "Server certificate config callback returned invalid (NULL) "
              "config.");
      return false;
    }
    gpr_log(GPR_DEBUG, "Using new server certificate config (%p).", config);

    size_t num_alpn_protocols = 0;
    const char** alpn_protocol_strings =
        grpc_fill_alpn_protocol_strings(&num_alpn_protocols);
    tsi_ssl_server_handshaker_factory* new_handshaker_factory = nullptr;
    const grpc_ssl_server_credentials* server_creds =
        static_cast<const grpc_ssl_server_credentials*>(this->server_creds());
    GPR_DEBUG_ASSERT(config->pem_root_certs != nullptr);
    tsi_ssl_server_handshaker_options options;
    options.pem_key_cert_pairs = grpc_convert_grpc_to_tsi_cert_pairs(
        config->pem_key_cert_pairs, config->num_key_cert_pairs);
    options.num_key_cert_pairs = config->num_key_cert_pairs;
    options.pem_client_root_certs = config->pem_root_certs;
    options.client_certificate_request =
        grpc_get_tsi_client_certificate_request_type(
            server_creds->config().client_certificate_request);
    options.cipher_suites = grpc_get_ssl_cipher_suites();
    options.alpn_protocols = alpn_protocol_strings;
    options.num_alpn_protocols = static_cast<uint16_t>(num_alpn_protocols);
    options.send_client_ca_list = server_creds->config().send_client_ca_list;
    tsi_result result = tsi_create_ssl_server_handshaker_factory_with_options(
        &options, &new_handshaker_factory);
    grpc_tsi_ssl_pem_key_cert_pairs_destroy(
        const_cast<tsi_ssl_pem_key_cert_pair*>(options.pem_key_cert_pairs),
        options.num_key_cert_pairs);
    gpr_free(alpn_protocol_strings);

    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
              tsi_result_to_string(result));
      return false;
    }
    set_server_handshaker_factory(new_handshaker_factory);
    return true;
  }

  void set_server_handshaker_factory(
      tsi_ssl_server_handshaker_factory* new_factory) {
    if (server_handshaker_factory_) {
      tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
    }
    server_handshaker_factory_ = new_factory;
  }

  grpc_core::Mutex mu_;
  tsi_ssl_server_handshaker_factory* server_handshaker_factory_ = nullptr;
};
}  // namespace

grpc_core::RefCountedPtr<grpc_channel_security_connector>
grpc_ssl_channel_security_connector_create(
    grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
    grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const grpc_ssl_config* config, const char* target_name,
    const char* overridden_target_name,
    tsi_ssl_session_cache* ssl_session_cache) {
  if (config == nullptr || target_name == nullptr) {
    gpr_log(GPR_ERROR, "An ssl channel needs a config and a target name.");
    return nullptr;
  }

  const char* pem_root_certs;
  const tsi_ssl_root_certs_store* root_store;
  if (config->pem_root_certs == nullptr) {
    // Use default root certificates.
    pem_root_certs = grpc_core::DefaultSslRootStore::GetPemRootCerts();
    if (pem_root_certs == nullptr) {
      gpr_log(GPR_ERROR, "Could not get default pem root certs.");
      return nullptr;
    }
    root_store = grpc_core::DefaultSslRootStore::GetRootStore();
  } else {
    pem_root_certs = config->pem_root_certs;
    root_store = nullptr;
  }

  grpc_core::RefCountedPtr<grpc_ssl_channel_security_connector> c =
      grpc_core::MakeRefCounted<grpc_ssl_channel_security_connector>(
          std::move(channel_creds), std::move(request_metadata_creds), config,
          target_name, overridden_target_name);
  const grpc_security_status result = c->InitializeHandshakerFactory(
      config, pem_root_certs, root_store, ssl_session_cache);
  if (result != GRPC_SECURITY_OK) {
    return nullptr;
  }
  return c;
}

grpc_core::RefCountedPtr<grpc_server_security_connector>
grpc_ssl_server_security_connector_create(
    grpc_core::RefCountedPtr<grpc_server_credentials> server_credentials) {
  GPR_ASSERT(server_credentials != nullptr);
  grpc_core::RefCountedPtr<grpc_ssl_server_security_connector> c =
      grpc_core::MakeRefCounted<grpc_ssl_server_security_connector>(
          std::move(server_credentials));
  const grpc_security_status retval = c->InitializeHandshakerFactory();
  if (retval != GRPC_SECURITY_OK) {
    return nullptr;
  }
  return c;
}
