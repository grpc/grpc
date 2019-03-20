/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/security_connector/ssl/ssl_security_connector.h"

#include <stdbool.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/transport/chttp2/alpn/alpn.h"
#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "src/core/lib/security/security_connector/load_system_roots.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"

namespace {

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
        overridden_target_name_(overridden_target_name == nullptr
                                    ? nullptr
                                    : gpr_strdup(overridden_target_name)),
        verify_options_(&config->verify_options) {
    char* port;
    gpr_split_host_port(target_name, &target_name_, &port);
    gpr_free(port);
  }

  ~grpc_ssl_channel_security_connector() override {
    tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
    if (target_name_ != nullptr) gpr_free(target_name_);
    if (overridden_target_name_ != nullptr) gpr_free(overridden_target_name_);
  }

  grpc_security_status InitializeHandshakerFactory(
      const grpc_ssl_config* config, tsi_ssl_session_cache* ssl_session_cache) {
    return grpc_ssl_tsi_client_handshaker_factory_init(
        config->pem_key_cert_pair, config->pem_root_certs, ssl_session_cache,
        &client_handshaker_factory_);
  }

  void add_handshakers(grpc_pollset_set* interested_parties,
                       grpc_core::HandshakeManager* handshake_mgr) override {
    // Instantiate TSI handshaker.
    tsi_handshaker* tsi_hs = nullptr;
    tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
        client_handshaker_factory_,
        overridden_target_name_ != nullptr ? overridden_target_name_
                                           : target_name_,
        &tsi_hs);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
              tsi_result_to_string(result));
      return;
    }
    // Create handshakers.
    handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(tsi_hs, this));
  }

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    const char* target_name = overridden_target_name_ != nullptr
                                  ? overridden_target_name_
                                  : target_name_;
    grpc_error* error = grpc_ssl_check_alpn(&peer);
    if (error == GRPC_ERROR_NONE) {
      error = grpc_ssl_check_peer_name(target_name, &peer);
      if (error == GRPC_ERROR_NONE) {
        if (verify_options_->verify_peer_callback != nullptr) {
          const tsi_peer_property* p =
              tsi_peer_get_property_by_name(&peer, TSI_X509_PEM_CERT_PROPERTY);
          if (p == nullptr) {
            error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
                "Cannot check peer: missing pem cert property.");
          } else {
            char* peer_pem =
                static_cast<char*>(gpr_malloc(p->value.length + 1));
            memcpy(peer_pem, p->value.data, p->value.length);
            peer_pem[p->value.length] = '\0';
            int callback_status = verify_options_->verify_peer_callback(
                target_name, peer_pem,
                verify_options_->verify_peer_callback_userdata);
            gpr_free(peer_pem);
            if (callback_status) {
              char* msg;
              gpr_asprintf(&msg, "Verify peer callback returned a failure (%d)",
                           callback_status);
              error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
              gpr_free(msg);
            }
          }
        }
        *auth_context = grpc_ssl_peer_to_auth_context(&peer);
      }
    }
    GRPC_CLOSURE_SCHED(on_peer_checked, error);
    tsi_peer_destruct(&peer);
  }

  int cmp(const grpc_security_connector* other_sc) const override {
    auto* other =
        reinterpret_cast<const grpc_ssl_channel_security_connector*>(other_sc);
    int c = channel_security_connector_cmp(other);
    if (c != 0) return c;
    return grpc_ssl_cmp_target_name(target_name_, other->target_name_,
                                    overridden_target_name_,
                                    other->overridden_target_name_);
  }

  bool check_call_host(const char* host, grpc_auth_context* auth_context,
                       grpc_closure* on_call_host_checked,
                       grpc_error** error) override {
    return grpc_ssl_check_call_host(host, target_name_, overridden_target_name_,
                                    auth_context, on_call_host_checked, error);
  }

  void cancel_check_call_host(grpc_closure* on_call_host_checked,
                              grpc_error* error) override {
    GRPC_ERROR_UNREF(error);
  }

 private:
  tsi_ssl_client_handshaker_factory* client_handshaker_factory_;
  char* target_name_;
  char* overridden_target_name_;
  const verify_peer_options* verify_options_;
};

class grpc_ssl_server_security_connector
    : public grpc_server_security_connector {
 public:
  grpc_ssl_server_security_connector(
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
    grpc_security_status retval = GRPC_SECURITY_OK;
    if (has_cert_config_fetcher()) {
      // Load initial credentials from certificate_config_fetcher:
      if (!try_fetch_ssl_server_credentials()) {
        gpr_log(GPR_ERROR,
                "Failed loading SSL server credentials from fetcher.");
        retval = GRPC_SECURITY_ERROR;
      }
    } else {
      auto* server_credentials =
          static_cast<const grpc_ssl_server_credentials*>(server_creds());
      retval = grpc_ssl_tsi_server_handshaker_factory_init(
          server_credentials->config().pem_key_cert_pairs,
          server_credentials->config().num_key_cert_pairs,
          server_credentials->config().pem_root_certs,
          server_credentials->config().client_certificate_request,
          &server_handshaker_factory_);
    }
    return retval;
  }

  void add_handshakers(grpc_pollset_set* interested_parties,
                       grpc_core::HandshakeManager* handshake_mgr) override {
    // Instantiate TSI handshaker.
    try_fetch_ssl_server_credentials();
    tsi_handshaker* tsi_hs = nullptr;
    tsi_result result = tsi_ssl_server_handshaker_factory_create_handshaker(
        server_handshaker_factory_, &tsi_hs);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
              tsi_result_to_string(result));
      return;
    }
    // Create handshakers.
    handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(tsi_hs, this));
  }

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override {
    grpc_error* error = grpc_ssl_check_alpn(&peer);
    *auth_context = grpc_ssl_peer_to_auth_context(&peer);
    tsi_peer_destruct(&peer);
    GRPC_CLOSURE_SCHED(on_peer_checked, error);
  }

  int cmp(const grpc_security_connector* other) const override {
    return server_security_connector_cmp(
        static_cast<const grpc_server_security_connector*>(other));
  }

 private:
  /* Attempts to fetch the server certificate config if a callback is available.
   * Current certificate config will continue to be used if the callback returns
   * an error. Returns true if new credentials were sucessfully loaded. */
  bool try_fetch_ssl_server_credentials() {
    grpc_ssl_server_certificate_config* certificate_config = nullptr;
    bool status;
    if (!has_cert_config_fetcher()) return false;
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

  /* Attempts to replace the server_handshaker_factory with a new factory using
   * the provided grpc_ssl_server_certificate_config. Should new factory
   * creation fail, the existing factory will not be replaced. Returns true on
   * success (new factory created). */
  bool try_replace_server_handshaker_factory(
      const grpc_ssl_server_certificate_config* config) {
    if (config == nullptr) {
      gpr_log(GPR_ERROR,
              "Server certificate config callback returned invalid (NULL) "
              "config.");
      return false;
    }
    tsi_ssl_pem_key_cert_pair* pem_key_cert_pairs =
        grpc_convert_grpc_to_tsi_cert_pairs(config->pem_key_cert_pairs,
                                            config->num_key_cert_pairs);
    const grpc_ssl_server_credentials* server_credentials =
        static_cast<const grpc_ssl_server_credentials*>(this->server_creds());
    tsi_ssl_server_handshaker_factory* new_handshaker_factory = nullptr;
    grpc_security_status retval = grpc_ssl_tsi_server_handshaker_factory_init(
        pem_key_cert_pairs, config->num_key_cert_pairs, config->pem_root_certs,
        server_credentials->config().client_certificate_request,
        &new_handshaker_factory);
    gpr_free(pem_key_cert_pairs);
    if (retval != GRPC_SECURITY_OK) {
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
  if (config->pem_root_certs == nullptr &&
      grpc_core::DefaultSslRootStore::GetPemRootCerts() == nullptr) {
    gpr_log(GPR_ERROR, "Could not get pem root certs.");
    return nullptr;
  }
  grpc_core::RefCountedPtr<grpc_ssl_channel_security_connector> c =
      grpc_core::MakeRefCounted<grpc_ssl_channel_security_connector>(
          std::move(channel_creds), std::move(request_metadata_creds), config,
          target_name, overridden_target_name);
  const grpc_security_status result =
      c->InitializeHandshakerFactory(config, ssl_session_cache);
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
