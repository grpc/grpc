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

#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"

#include <stdbool.h>
#include <string.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "src/core/lib/security/credentials/tls/tls_credentials.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"

namespace grpc_core {

namespace {

tsi_ssl_pem_key_cert_pair* ConvertToTsiPemKeyCertPair(
    const grpc_tls_key_materials_config::PemKeyCertPairList& cert_pair_list) {
  tsi_ssl_pem_key_cert_pair* tsi_pairs = nullptr;
  size_t num_key_cert_pairs = cert_pair_list.size();
  if (num_key_cert_pairs > 0) {
    GPR_ASSERT(cert_pair_list.data() != nullptr);
    tsi_pairs = static_cast<tsi_ssl_pem_key_cert_pair*>(
        gpr_zalloc(num_key_cert_pairs * sizeof(tsi_ssl_pem_key_cert_pair)));
  }
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    GPR_ASSERT(cert_pair_list[i].private_key() != nullptr);
    GPR_ASSERT(cert_pair_list[i].cert_chain() != nullptr);
    tsi_pairs[i].cert_chain = gpr_strdup(cert_pair_list[i].cert_chain());
    tsi_pairs[i].private_key = gpr_strdup(cert_pair_list[i].private_key());
  }
  return tsi_pairs;
}

}  // namespace

grpc_status_code TlsFetchKeyMaterials(
    const grpc_core::RefCountedPtr<grpc_tls_key_materials_config>&
        key_materials_config,
    const grpc_tls_credentials_options& options, bool is_server,
    grpc_ssl_certificate_config_reload_status* status) {
  GPR_ASSERT(key_materials_config != nullptr);
  GPR_ASSERT(status != nullptr);
  bool is_key_materials_empty =
      key_materials_config->pem_key_cert_pair_list().empty();
  grpc_tls_credential_reload_config* credential_reload_config =
      options.credential_reload_config();
  /** If there are no key materials and no credential reload config and the
   *  caller is a server, then return an error. We do not require that a client
   *  always provision certificates. **/
  if (credential_reload_config == nullptr && is_key_materials_empty &&
      is_server) {
    gpr_log(GPR_ERROR,
            "Either credential reload config or key materials should be "
            "provisioned.");
    return GRPC_STATUS_FAILED_PRECONDITION;
  }
  grpc_status_code reload_status = GRPC_STATUS_OK;
  /** Use |credential_reload_config| to update |key_materials_config|. **/
  if (credential_reload_config != nullptr) {
    grpc_tls_credential_reload_arg* arg = new grpc_tls_credential_reload_arg();
    arg->key_materials_config = key_materials_config.get();
    arg->error_details = new grpc_tls_error_details();
    int result = credential_reload_config->Schedule(arg);
    if (result) {
      /** Credential reloading is performed async. This is not yet supported.
       * **/
      gpr_log(GPR_ERROR, "Async credential reload is unsupported now.");
      *status = GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
      reload_status =
          is_key_materials_empty ? GRPC_STATUS_UNIMPLEMENTED : GRPC_STATUS_OK;
    } else {
      /** Credential reloading is performed sync. **/
      *status = arg->status;
      if (arg->status == GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED) {
        /* Key materials is not empty. */
        gpr_log(GPR_DEBUG, "Credential does not change after reload.");
      } else if (arg->status == GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL) {
        gpr_log(GPR_ERROR, "Credential reload failed with an error:");
        if (arg->error_details != nullptr) {
          gpr_log(GPR_ERROR, "%s", arg->error_details->error_details().c_str());
        }
        reload_status =
            is_key_materials_empty ? GRPC_STATUS_INTERNAL : GRPC_STATUS_OK;
      }
    }
    delete arg->error_details;
    /** If the credential reload config was constructed via a wrapped language,
     *  then |arg->context| and |arg->destroy_context| will not be nullptr. In
     *  this case, we must destroy |arg->context|, which stores the wrapped
     *  language-version of the credential reload arg. **/
    if (arg->destroy_context != nullptr) {
      arg->destroy_context(arg->context);
    }
    delete arg;
  }
  return reload_status;
}

grpc_error* TlsCheckHostName(const char* peer_name, const tsi_peer* peer) {
  /* Check the peer name if specified. */
  if (peer_name != nullptr && !grpc_ssl_host_matches_name(peer, peer_name)) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Peer name ", peer_name, " is not in peer certificate")
            .c_str());
  }
  return GRPC_ERROR_NONE;
}

TlsChannelSecurityConnector::TlsChannelSecurityConnector(
    grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
    grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const char* target_name, const char* overridden_target_name)
    : grpc_channel_security_connector(GRPC_SSL_URL_SCHEME,
                                      std::move(channel_creds),
                                      std::move(request_metadata_creds)),
      overridden_target_name_(
          overridden_target_name == nullptr ? "" : overridden_target_name) {
  key_materials_config_ = grpc_tls_key_materials_config_create()->Ref();
  check_arg_ = ServerAuthorizationCheckArgCreate(this);
  absl::string_view host;
  absl::string_view port;
  grpc_core::SplitHostPort(target_name, &host, &port);
  target_name_ = std::string(host);
}

TlsChannelSecurityConnector::~TlsChannelSecurityConnector() {
  if (client_handshaker_factory_ != nullptr) {
    tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
  }
  if (key_materials_config_.get() != nullptr) {
    key_materials_config_.get()->Unref();
  }
  ServerAuthorizationCheckArgDestroy(check_arg_);
}

void TlsChannelSecurityConnector::add_handshakers(
    const grpc_channel_args* args, grpc_pollset_set* /*interested_parties*/,
    grpc_core::HandshakeManager* handshake_mgr) {
  if (RefreshHandshakerFactory() != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory refresh failed.");
    return;
  }
  // Instantiate TSI handshaker.
  tsi_handshaker* tsi_hs = nullptr;
  tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
      client_handshaker_factory_,
      overridden_target_name_.empty() ? target_name_.c_str()
                                      : overridden_target_name_.c_str(),
      &tsi_hs);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    return;
  }
  // Create handshakers.
  handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(tsi_hs, this, args));
}

void TlsChannelSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* /*ep*/,
    grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  const char* target_name = overridden_target_name_.empty()
                                ? target_name_.c_str()
                                : overridden_target_name_.c_str();
  grpc_error* error = grpc_ssl_check_alpn(&peer);
  if (error != GRPC_ERROR_NONE) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
    tsi_peer_destruct(&peer);
    return;
  }
  *auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  const TlsCredentials* creds =
      static_cast<const TlsCredentials*>(channel_creds());
  if (creds->options().server_verification_option() ==
      GRPC_TLS_SERVER_VERIFICATION) {
    /* Do the default host name check if specifying the target name. */
    error = TlsCheckHostName(target_name, &peer);
    if (error != GRPC_ERROR_NONE) {
      grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
      tsi_peer_destruct(&peer);
      return;
    }
  }
  /* Do the custom server authorization check, if specified by the user. */
  const grpc_tls_server_authorization_check_config* config =
      creds->options().server_authorization_check_config();
  /* If server authorization config is not null, use it to perform
   * server authorization check. */
  if (config != nullptr) {
    const tsi_peer_property* p =
        tsi_peer_get_property_by_name(&peer, TSI_X509_PEM_CERT_PROPERTY);
    if (p == nullptr) {
      error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
          "Cannot check peer: missing pem cert property.");
    } else {
      char* peer_pem = static_cast<char*>(gpr_zalloc(p->value.length + 1));
      memcpy(peer_pem, p->value.data, p->value.length);
      GPR_ASSERT(check_arg_ != nullptr);
      check_arg_->peer_cert = check_arg_->peer_cert == nullptr
                                  ? gpr_strdup(peer_pem)
                                  : check_arg_->peer_cert;
      check_arg_->target_name = check_arg_->target_name == nullptr
                                    ? gpr_strdup(target_name)
                                    : check_arg_->target_name;
      on_peer_checked_ = on_peer_checked;
      gpr_free(peer_pem);
      const tsi_peer_property* chain = tsi_peer_get_property_by_name(
          &peer, TSI_X509_PEM_CERT_CHAIN_PROPERTY);
      if (chain != nullptr) {
        char* peer_pem_chain =
            static_cast<char*>(gpr_zalloc(chain->value.length + 1));
        memcpy(peer_pem_chain, chain->value.data, chain->value.length);
        check_arg_->peer_cert_full_chain =
            check_arg_->peer_cert_full_chain == nullptr
                ? gpr_strdup(peer_pem_chain)
                : check_arg_->peer_cert_full_chain;
        gpr_free(peer_pem_chain);
      }
      int callback_status = config->Schedule(check_arg_);
      /* Server authorization check is handled asynchronously. */
      if (callback_status) {
        tsi_peer_destruct(&peer);
        return;
      }
      /* Server authorization check is handled synchronously. */
      error = ProcessServerAuthorizationCheckResult(check_arg_);
    }
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
  tsi_peer_destruct(&peer);
}

int TlsChannelSecurityConnector::cmp(
    const grpc_security_connector* other_sc) const {
  auto* other = reinterpret_cast<const TlsChannelSecurityConnector*>(other_sc);
  int c = channel_security_connector_cmp(other);
  if (c != 0) {
    return c;
  }
  return grpc_ssl_cmp_target_name(
      target_name_.c_str(), other->target_name_.c_str(),
      overridden_target_name_.c_str(), other->overridden_target_name_.c_str());
}

bool TlsChannelSecurityConnector::check_call_host(
    absl::string_view host, grpc_auth_context* auth_context,
    grpc_closure* /*on_call_host_checked*/, grpc_error** error) {
  return grpc_ssl_check_call_host(host, target_name_.c_str(),
                                  overridden_target_name_.c_str(), auth_context,
                                  error);
}

void TlsChannelSecurityConnector::cancel_check_call_host(
    grpc_closure* /*on_call_host_checked*/, grpc_error* error) {
  GRPC_ERROR_UNREF(error);
}

grpc_core::RefCountedPtr<grpc_channel_security_connector>
TlsChannelSecurityConnector::CreateTlsChannelSecurityConnector(
    grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
    grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const char* target_name, const char* overridden_target_name,
    tsi_ssl_session_cache* ssl_session_cache) {
  if (channel_creds == nullptr) {
    gpr_log(GPR_ERROR,
            "channel_creds is nullptr in "
            "TlsChannelSecurityConnectorCreate()");
    return nullptr;
  }
  if (target_name == nullptr) {
    gpr_log(GPR_ERROR,
            "target_name is nullptr in "
            "TlsChannelSecurityConnectorCreate()");
    return nullptr;
  }
  grpc_core::RefCountedPtr<TlsChannelSecurityConnector> c =
      grpc_core::MakeRefCounted<TlsChannelSecurityConnector>(
          std::move(channel_creds), std::move(request_metadata_creds),
          target_name, overridden_target_name);
  if (c->InitializeHandshakerFactory(ssl_session_cache) != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR, "Could not initialize client handshaker factory.");
    return nullptr;
  }
  return c;
}

grpc_security_status TlsChannelSecurityConnector::ReplaceHandshakerFactory(
    tsi_ssl_session_cache* ssl_session_cache) {
  const TlsCredentials* creds =
      static_cast<const TlsCredentials*>(channel_creds());
  bool skip_server_certificate_verification =
      creds->options().server_verification_option() ==
      GRPC_TLS_SKIP_ALL_SERVER_VERIFICATION;
  /* Free the client handshaker factory if exists. */
  if (client_handshaker_factory_) {
    tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
  }
  tsi_ssl_pem_key_cert_pair* pem_key_cert_pair = ConvertToTsiPemKeyCertPair(
      key_materials_config_->pem_key_cert_pair_list());
  grpc_security_status status = grpc_ssl_tsi_client_handshaker_factory_init(
      pem_key_cert_pair, key_materials_config_->pem_root_certs(),
      skip_server_certificate_verification,
      grpc_get_tsi_tls_version(creds->options().min_tls_version()),
      grpc_get_tsi_tls_version(creds->options().max_tls_version()),
      ssl_session_cache, &client_handshaker_factory_);
  /* Free memory. */
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(pem_key_cert_pair, 1);
  return status;
}

grpc_security_status TlsChannelSecurityConnector::InitializeHandshakerFactory(
    tsi_ssl_session_cache* ssl_session_cache) {
  grpc_core::MutexLock lock(&mu_);
  const TlsCredentials* creds =
      static_cast<const TlsCredentials*>(channel_creds());
  grpc_tls_key_materials_config* key_materials_config =
      creds->options().key_materials_config();
  // key_materials_config_->set_key_materials will handle the copying of the key
  // materials users provided
  if (key_materials_config != nullptr) {
    key_materials_config_->set_key_materials(
        key_materials_config->pem_root_certs(),
        key_materials_config->pem_key_cert_pair_list());
  }
  grpc_ssl_certificate_config_reload_status reload_status =
      GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  /** If |creds->options()| has a credential reload config, then the call to
   *  |TlsFetchKeyMaterials| will use it to update the root cert and
   *  pem-key-cert-pair list stored in |key_materials_config_|. **/
  if (TlsFetchKeyMaterials(key_materials_config_, creds->options(), false,
                           &reload_status) != GRPC_STATUS_OK) {
    /* Raise an error if key materials are not populated. */
    return GRPC_SECURITY_ERROR;
  }
  return ReplaceHandshakerFactory(ssl_session_cache);
}

grpc_security_status TlsChannelSecurityConnector::RefreshHandshakerFactory() {
  grpc_core::MutexLock lock(&mu_);
  const TlsCredentials* creds =
      static_cast<const TlsCredentials*>(channel_creds());
  grpc_ssl_certificate_config_reload_status reload_status =
      GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  /** If |creds->options()| has a credential reload config, then the call to
   *  |TlsFetchKeyMaterials| will use it to update the root cert and
   *  pem-key-cert-pair list stored in |key_materials_config_|. **/
  if (TlsFetchKeyMaterials(key_materials_config_, creds->options(), false,
                           &reload_status) != GRPC_STATUS_OK) {
    return GRPC_SECURITY_ERROR;
  }
  if (reload_status != GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW) {
    // Re-use existing handshaker factory.
    return GRPC_SECURITY_OK;
  } else {
    return ReplaceHandshakerFactory(nullptr);
  }
}

void TlsChannelSecurityConnector::ServerAuthorizationCheckDone(
    grpc_tls_server_authorization_check_arg* arg) {
  GPR_ASSERT(arg != nullptr);
  grpc_core::ExecCtx exec_ctx;
  grpc_error* error = ProcessServerAuthorizationCheckResult(arg);
  TlsChannelSecurityConnector* connector =
      static_cast<TlsChannelSecurityConnector*>(arg->cb_user_data);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, connector->on_peer_checked_, error);
}

grpc_error* TlsChannelSecurityConnector::ProcessServerAuthorizationCheckResult(
    grpc_tls_server_authorization_check_arg* arg) {
  grpc_error* error = GRPC_ERROR_NONE;
  /* Server authorization check is cancelled by caller. */
  if (arg->status == GRPC_STATUS_CANCELLED) {
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Server authorization check is cancelled by the caller "
                     "with error: ",
                     arg->error_details->error_details())
            .c_str());
  } else if (arg->status == GRPC_STATUS_OK) {
    /* Server authorization check completed successfully but returned check
     * failure. */
    if (!arg->success) {
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("Server authorization check failed with error: ",
                       arg->error_details->error_details())
              .c_str());
    }
    /* Server authorization check did not complete correctly. */
  } else {
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat(
            "Server authorization check did not finish correctly with error: ",
            arg->error_details->error_details())
            .c_str());
  }
  return error;
}

grpc_tls_server_authorization_check_arg*
TlsChannelSecurityConnector::ServerAuthorizationCheckArgCreate(
    void* user_data) {
  grpc_tls_server_authorization_check_arg* arg =
      new grpc_tls_server_authorization_check_arg();
  arg->error_details = new grpc_tls_error_details();
  arg->cb = ServerAuthorizationCheckDone;
  arg->cb_user_data = user_data;
  arg->status = GRPC_STATUS_OK;
  return arg;
}

void TlsChannelSecurityConnector::ServerAuthorizationCheckArgDestroy(
    grpc_tls_server_authorization_check_arg* arg) {
  if (arg == nullptr) {
    return;
  }
  gpr_free((void*)arg->target_name);
  gpr_free((void*)arg->peer_cert);
  if (arg->peer_cert_full_chain) gpr_free((void*)arg->peer_cert_full_chain);
  delete arg->error_details;
  if (arg->destroy_context != nullptr) {
    arg->destroy_context(arg->context);
  }
  delete arg;
}

TlsServerSecurityConnector::TlsServerSecurityConnector(
    grpc_core::RefCountedPtr<grpc_server_credentials> server_creds)
    : grpc_server_security_connector(GRPC_SSL_URL_SCHEME,
                                     std::move(server_creds)) {
  key_materials_config_ = grpc_tls_key_materials_config_create()->Ref();
}

TlsServerSecurityConnector::~TlsServerSecurityConnector() {
  if (server_handshaker_factory_ != nullptr) {
    tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
  }
  if (key_materials_config_.get() != nullptr) {
    key_materials_config_.get()->Unref();
  }
}

void TlsServerSecurityConnector::add_handshakers(
    const grpc_channel_args* args, grpc_pollset_set* /*interested_parties*/,
    grpc_core::HandshakeManager* handshake_mgr) {
  /* Refresh handshaker factory if needed. */
  if (RefreshHandshakerFactory() != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR, "Handshaker factory refresh failed.");
    return;
  }
  /* Create a TLS TSI handshaker for server. */
  tsi_handshaker* tsi_hs = nullptr;
  tsi_result result = tsi_ssl_server_handshaker_factory_create_handshaker(
      server_handshaker_factory_, &tsi_hs);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    return;
  }
  handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(tsi_hs, this, args));
}

void TlsServerSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* /*ep*/,
    grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  grpc_error* error = grpc_ssl_check_alpn(&peer);
  *auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  tsi_peer_destruct(&peer);
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
}

int TlsServerSecurityConnector::cmp(
    const grpc_security_connector* other) const {
  return server_security_connector_cmp(
      static_cast<const grpc_server_security_connector*>(other));
}

grpc_core::RefCountedPtr<grpc_server_security_connector>
TlsServerSecurityConnector::CreateTlsServerSecurityConnector(
    grpc_core::RefCountedPtr<grpc_server_credentials> server_creds) {
  if (server_creds == nullptr) {
    gpr_log(GPR_ERROR,
            "server_creds is nullptr in "
            "TlsServerSecurityConnectorCreate()");
    return nullptr;
  }
  grpc_core::RefCountedPtr<TlsServerSecurityConnector> c =
      grpc_core::MakeRefCounted<TlsServerSecurityConnector>(
          std::move(server_creds));
  if (c->InitializeHandshakerFactory() != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR, "Could not initialize server handshaker factory.");
    return nullptr;
  }
  return c;
}

grpc_security_status TlsServerSecurityConnector::ReplaceHandshakerFactory() {
  const TlsServerCredentials* creds =
      static_cast<const TlsServerCredentials*>(server_creds());
  /* Free the server handshaker factory if exists. */
  if (server_handshaker_factory_) {
    tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
  }
  GPR_ASSERT(!key_materials_config_->pem_key_cert_pair_list().empty());
  tsi_ssl_pem_key_cert_pair* pem_key_cert_pairs = ConvertToTsiPemKeyCertPair(
      key_materials_config_->pem_key_cert_pair_list());
  size_t num_key_cert_pairs =
      key_materials_config_->pem_key_cert_pair_list().size();
  grpc_security_status status = grpc_ssl_tsi_server_handshaker_factory_init(
      pem_key_cert_pairs, num_key_cert_pairs,
      key_materials_config_->pem_root_certs(),
      creds->options().cert_request_type(),
      grpc_get_tsi_tls_version(creds->options().min_tls_version()),
      grpc_get_tsi_tls_version(creds->options().max_tls_version()),
      &server_handshaker_factory_);
  /* Free memory. */
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(pem_key_cert_pairs,
                                          num_key_cert_pairs);
  return status;
}

grpc_security_status TlsServerSecurityConnector::InitializeHandshakerFactory() {
  grpc_core::MutexLock lock(&mu_);
  const TlsServerCredentials* creds =
      static_cast<const TlsServerCredentials*>(server_creds());
  grpc_tls_key_materials_config* key_materials_config =
      creds->options().key_materials_config();
  if (key_materials_config != nullptr) {
    key_materials_config_->set_key_materials(
        key_materials_config->pem_root_certs(),
        key_materials_config->pem_key_cert_pair_list());
  }
  grpc_ssl_certificate_config_reload_status reload_status =
      GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  /** If |creds->options()| has a credential reload config, then the call to
   *  |TlsFetchKeyMaterials| will use it to update the root cert and
   *  pem-key-cert-pair list stored in |key_materials_config_|. Otherwise, it
   *  will return |GRPC_STATUS_OK| if |key_materials_config_| already has
   *  credentials, and an error code if not. **/
  if (TlsFetchKeyMaterials(key_materials_config_, creds->options(), true,
                           &reload_status) != GRPC_STATUS_OK) {
    /* Raise an error if key materials are not populated. */
    return GRPC_SECURITY_ERROR;
  }
  return ReplaceHandshakerFactory();
}

grpc_security_status TlsServerSecurityConnector::RefreshHandshakerFactory() {
  grpc_core::MutexLock lock(&mu_);
  const TlsServerCredentials* creds =
      static_cast<const TlsServerCredentials*>(server_creds());
  grpc_ssl_certificate_config_reload_status reload_status =
      GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED;
  /** If |creds->options()| has a credential reload config, then the call to
   *  |TlsFetchKeyMaterials| will use it to update the root cert and
   *  pem-key-cert-pair list stored in |key_materials_config_|. Otherwise, it
   *  will return |GRPC_STATUS_OK| if |key_materials_config_| already has
   *  credentials, and an error code if not. **/
  if (TlsFetchKeyMaterials(key_materials_config_, creds->options(), true,
                           &reload_status) != GRPC_STATUS_OK) {
    return GRPC_SECURITY_ERROR;
  }
  if (reload_status != GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW) {
    /* At this point, we should have key materials populated. */
    return GRPC_SECURITY_OK;
  } else {
    return ReplaceHandshakerFactory();
  }
}

}  // namespace grpc_core
