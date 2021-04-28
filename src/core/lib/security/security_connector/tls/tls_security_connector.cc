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
    const PemKeyCertPairList& cert_pair_list) {
  tsi_ssl_pem_key_cert_pair* tsi_pairs = nullptr;
  size_t num_key_cert_pairs = cert_pair_list.size();
  if (num_key_cert_pairs > 0) {
    GPR_ASSERT(cert_pair_list.data() != nullptr);
    tsi_pairs = static_cast<tsi_ssl_pem_key_cert_pair*>(
        gpr_zalloc(num_key_cert_pairs * sizeof(tsi_ssl_pem_key_cert_pair)));
  }
  for (size_t i = 0; i < num_key_cert_pairs; i++) {
    GPR_ASSERT(!cert_pair_list[i].private_key().empty());
    GPR_ASSERT(!cert_pair_list[i].cert_chain().empty());
    tsi_pairs[i].cert_chain =
        gpr_strdup(cert_pair_list[i].cert_chain().c_str());
    tsi_pairs[i].private_key =
        gpr_strdup(cert_pair_list[i].private_key().c_str());
  }
  return tsi_pairs;
}

}  // namespace

// -------------------channel security connector-------------------
RefCountedPtr<grpc_channel_security_connector>
TlsChannelSecurityConnector::CreateTlsChannelSecurityConnector(
    RefCountedPtr<grpc_channel_credentials> channel_creds,
    RefCountedPtr<grpc_tls_credentials_options> options,
    RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const char* target_name, const char* overridden_target_name,
    tsi_ssl_session_cache* ssl_session_cache) {
  if (channel_creds == nullptr) {
    gpr_log(GPR_ERROR,
            "channel_creds is nullptr in "
            "TlsChannelSecurityConnectorCreate()");
    return nullptr;
  }
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "options is nullptr in "
            "TlsChannelSecurityConnectorCreate()");
    return nullptr;
  }
  if (target_name == nullptr) {
    gpr_log(GPR_ERROR,
            "target_name is nullptr in "
            "TlsChannelSecurityConnectorCreate()");
    return nullptr;
  }
  return MakeRefCounted<TlsChannelSecurityConnector>(
      std::move(channel_creds), std::move(options),
      std::move(request_metadata_creds), target_name, overridden_target_name,
      ssl_session_cache);
}

TlsChannelSecurityConnector::TlsChannelSecurityConnector(
    RefCountedPtr<grpc_channel_credentials> channel_creds,
    RefCountedPtr<grpc_tls_credentials_options> options,
    RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const char* target_name, const char* overridden_target_name,
    tsi_ssl_session_cache* ssl_session_cache)
    : grpc_channel_security_connector(GRPC_SSL_URL_SCHEME,
                                      std::move(channel_creds),
                                      std::move(request_metadata_creds)),
      options_(std::move(options)),
      overridden_target_name_(
          overridden_target_name == nullptr ? "" : overridden_target_name),
      ssl_session_cache_(ssl_session_cache) {
  if (ssl_session_cache_ != nullptr) {
    tsi_ssl_session_cache_ref(ssl_session_cache_);
  }
  check_arg_ = ServerAuthorizationCheckArgCreate(this);
  absl::string_view host;
  absl::string_view port;
  SplitHostPort(target_name, &host, &port);
  target_name_ = std::string(host);
  // Create a watcher.
  auto watcher_ptr = absl::make_unique<TlsChannelCertificateWatcher>(this);
  certificate_watcher_ = watcher_ptr.get();
  // Register the watcher with the distributor.
  grpc_tls_certificate_distributor* distributor =
      options_->certificate_distributor();
  absl::optional<std::string> watched_root_cert_name;
  if (options_->watch_root_cert()) {
    watched_root_cert_name = options_->root_cert_name();
  }
  absl::optional<std::string> watched_identity_cert_name;
  if (options_->watch_identity_pair()) {
    watched_identity_cert_name = options_->identity_cert_name();
  }
  // We will use the root certs stored in system default locations if not
  // watching root certs on the client side. We will handle this case
  // differently here, because "watching a default roots without the identity
  // certs" is a valid case(and hence we will need to call
  // OnCertificatesChanged), but it requires nothing from the provider, and
  // hence no need to register the watcher.
  bool use_default_roots = !options_->watch_root_cert();
  if (use_default_roots && !options_->watch_identity_pair()) {
    watcher_ptr->OnCertificatesChanged(absl::nullopt, absl::nullopt);
  } else {
    distributor->WatchTlsCertificates(std::move(watcher_ptr),
                                      watched_root_cert_name,
                                      watched_identity_cert_name);
  }
}

TlsChannelSecurityConnector::~TlsChannelSecurityConnector() {
  if (ssl_session_cache_ != nullptr) {
    tsi_ssl_session_cache_unref(ssl_session_cache_);
  }
  // Cancel all the watchers.
  grpc_tls_certificate_distributor* distributor =
      options_->certificate_distributor();
  if (distributor != nullptr) {
    distributor->CancelTlsCertificatesWatch(certificate_watcher_);
  }
  if (client_handshaker_factory_ != nullptr) {
    tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
  }
  if (check_arg_ != nullptr) {
    ServerAuthorizationCheckArgDestroy(check_arg_);
  }
}

void TlsChannelSecurityConnector::add_handshakers(
    const grpc_channel_args* args, grpc_pollset_set* /*interested_parties*/,
    HandshakeManager* handshake_mgr) {
  MutexLock lock(&mu_);
  if (client_handshaker_factory_ != nullptr) {
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
    handshake_mgr->Add(SecurityHandshakerCreate(tsi_hs, this, args));
    return;
  }
  // TODO(ZhenLian): Implement the logic(delegation to
  // BlockOnInitialCredentialHandshaker) when certificates are not ready.
  gpr_log(GPR_ERROR, "%s not supported yet.",
          "Client BlockOnInitialCredentialHandshaker");
}

void TlsChannelSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* /*ep*/,
    RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  const char* target_name = overridden_target_name_.empty()
                                ? target_name_.c_str()
                                : overridden_target_name_.c_str();
  grpc_error_handle error = grpc_ssl_check_alpn(&peer);
  if (error != GRPC_ERROR_NONE) {
    ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
    tsi_peer_destruct(&peer);
    return;
  }
  *auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  if (options_->server_verification_option() == GRPC_TLS_SERVER_VERIFICATION) {
    /* Do the default host name check if specifying the target name. */
    error = internal::TlsCheckHostName(target_name, &peer);
    if (error != GRPC_ERROR_NONE) {
      ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
      tsi_peer_destruct(&peer);
      return;
    }
  }
  /* Do the custom server authorization check, if specified by the user. */
  const grpc_tls_server_authorization_check_config* config =
      options_->server_authorization_check_config();
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
      // TODO(zhenlian) - This should be cleaned up as part of the custom
      // verification changes. Fill in the subject alternative names
      std::vector<char*> subject_alternative_names;
      for (size_t i = 0; i < peer.property_count; i++) {
        const tsi_peer_property* prop = &peer.properties[i];
        if (strcmp(prop->name,
                   TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) == 0) {
          char* san = new char[prop->value.length + 1];
          memcpy(san, prop->value.data, prop->value.length);
          san[prop->value.length] = '\0';
          subject_alternative_names.emplace_back(san);
        }
      }
      if (check_arg_->subject_alternative_names != nullptr) {
        for (size_t i = 0; i < check_arg_->subject_alternative_names_size;
             ++i) {
          delete[] check_arg_->subject_alternative_names[i];
        }
        delete[] check_arg_->subject_alternative_names;
      }
      check_arg_->subject_alternative_names_size =
          subject_alternative_names.size();
      if (subject_alternative_names.empty()) {
        check_arg_->subject_alternative_names = nullptr;
      } else {
        check_arg_->subject_alternative_names =
            new char*[check_arg_->subject_alternative_names_size];
        for (size_t i = 0; i < check_arg_->subject_alternative_names_size;
             ++i) {
          check_arg_->subject_alternative_names[i] =
              subject_alternative_names[i];
        }
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
  ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
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
    grpc_closure* /*on_call_host_checked*/, grpc_error_handle* error) {
  if (options_->server_verification_option() ==
          GRPC_TLS_SKIP_HOSTNAME_VERIFICATION ||
      options_->server_verification_option() ==
          GRPC_TLS_SKIP_ALL_SERVER_VERIFICATION) {
    return true;
  }
  return grpc_ssl_check_call_host(host, target_name_.c_str(),
                                  overridden_target_name_.c_str(), auth_context,
                                  error);
}

void TlsChannelSecurityConnector::cancel_check_call_host(
    grpc_closure* /*on_call_host_checked*/, grpc_error_handle error) {
  GRPC_ERROR_UNREF(error);
}

void TlsChannelSecurityConnector::TlsChannelCertificateWatcher::
    OnCertificatesChanged(absl::optional<absl::string_view> root_certs,
                          absl::optional<PemKeyCertPairList> key_cert_pairs) {
  GPR_ASSERT(security_connector_ != nullptr);
  MutexLock lock(&security_connector_->mu_);
  if (root_certs.has_value()) {
    security_connector_->pem_root_certs_ = root_certs;
  }
  if (key_cert_pairs.has_value()) {
    security_connector_->pem_key_cert_pair_list_ = std::move(key_cert_pairs);
  }
  const bool root_ready = !security_connector_->options_->watch_root_cert() ||
                          security_connector_->pem_root_certs_.has_value();
  const bool identity_ready =
      !security_connector_->options_->watch_identity_pair() ||
      security_connector_->pem_key_cert_pair_list_.has_value();
  if (root_ready && identity_ready) {
    if (security_connector_->UpdateHandshakerFactoryLocked() !=
        GRPC_SECURITY_OK) {
      gpr_log(GPR_ERROR, "Update handshaker factory failed.");
    }
  }
}

// TODO(ZhenLian): implement the logic to signal waiting handshakers once
// BlockOnInitialCredentialHandshaker is implemented.
void TlsChannelSecurityConnector::TlsChannelCertificateWatcher::OnError(
    grpc_error_handle root_cert_error, grpc_error_handle identity_cert_error) {
  if (root_cert_error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "TlsChannelCertificateWatcher getting root_cert_error: %s",
            grpc_error_std_string(root_cert_error).c_str());
  }
  if (identity_cert_error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "TlsChannelCertificateWatcher getting identity_cert_error: %s",
            grpc_error_std_string(identity_cert_error).c_str());
  }
  GRPC_ERROR_UNREF(root_cert_error);
  GRPC_ERROR_UNREF(identity_cert_error);
}

// TODO(ZhenLian): implement the logic to signal waiting handshakers once
// BlockOnInitialCredentialHandshaker is implemented.
grpc_security_status
TlsChannelSecurityConnector::UpdateHandshakerFactoryLocked() {
  bool skip_server_certificate_verification =
      options_->server_verification_option() ==
      GRPC_TLS_SKIP_ALL_SERVER_VERIFICATION;
  /* Free the client handshaker factory if exists. */
  if (client_handshaker_factory_ != nullptr) {
    tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
  }
  std::string pem_root_certs;
  if (pem_root_certs_.has_value()) {
    // TODO(ZhenLian): update the underlying TSI layer to use C++ types like
    // std::string and absl::string_view to avoid making another copy here.
    pem_root_certs = std::string(*pem_root_certs_);
  }
  tsi_ssl_pem_key_cert_pair* pem_key_cert_pair = nullptr;
  if (pem_key_cert_pair_list_.has_value()) {
    pem_key_cert_pair = ConvertToTsiPemKeyCertPair(*pem_key_cert_pair_list_);
  }
  bool use_default_roots = !options_->watch_root_cert();
  grpc_security_status status = grpc_ssl_tsi_client_handshaker_factory_init(
      pem_key_cert_pair,
      pem_root_certs.empty() || use_default_roots ? nullptr
                                                  : pem_root_certs.c_str(),
      skip_server_certificate_verification,
      grpc_get_tsi_tls_version(options_->min_tls_version()),
      grpc_get_tsi_tls_version(options_->max_tls_version()), ssl_session_cache_,
      &client_handshaker_factory_);
  /* Free memory. */
  if (pem_key_cert_pair != nullptr) {
    grpc_tsi_ssl_pem_key_cert_pairs_destroy(pem_key_cert_pair, 1);
  }
  return status;
}

void TlsChannelSecurityConnector::ServerAuthorizationCheckDone(
    grpc_tls_server_authorization_check_arg* arg) {
  GPR_ASSERT(arg != nullptr);
  ExecCtx exec_ctx;
  grpc_error_handle error = ProcessServerAuthorizationCheckResult(arg);
  TlsChannelSecurityConnector* connector =
      static_cast<TlsChannelSecurityConnector*>(arg->cb_user_data);
  ExecCtx::Run(DEBUG_LOCATION, connector->on_peer_checked_, error);
}

grpc_error_handle
TlsChannelSecurityConnector::ProcessServerAuthorizationCheckResult(
    grpc_tls_server_authorization_check_arg* arg) {
  grpc_error_handle error = GRPC_ERROR_NONE;
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
  arg->target_name = nullptr;
  arg->peer_cert = nullptr;
  arg->peer_cert_full_chain = nullptr;
  arg->subject_alternative_names = nullptr;
  arg->subject_alternative_names_size = 0;
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
  gpr_free(const_cast<char*>(arg->target_name));
  gpr_free(const_cast<char*>(arg->peer_cert));
  gpr_free(const_cast<char*>(arg->peer_cert_full_chain));
  for (size_t i = 0; i < arg->subject_alternative_names_size; ++i) {
    delete[] arg->subject_alternative_names[i];
  }
  delete[] arg->subject_alternative_names;
  delete arg->error_details;
  if (arg->destroy_context != nullptr) {
    arg->destroy_context(arg->context);
  }
  delete arg;
}

// -------------------server security connector-------------------
RefCountedPtr<grpc_server_security_connector>
TlsServerSecurityConnector::CreateTlsServerSecurityConnector(
    RefCountedPtr<grpc_server_credentials> server_creds,
    RefCountedPtr<grpc_tls_credentials_options> options) {
  if (server_creds == nullptr) {
    gpr_log(GPR_ERROR,
            "server_creds is nullptr in "
            "TlsServerSecurityConnectorCreate()");
    return nullptr;
  }
  if (options == nullptr) {
    gpr_log(GPR_ERROR,
            "options is nullptr in "
            "TlsServerSecurityConnectorCreate()");
    return nullptr;
  }
  return MakeRefCounted<TlsServerSecurityConnector>(std::move(server_creds),
                                                    std::move(options));
}

TlsServerSecurityConnector::TlsServerSecurityConnector(
    RefCountedPtr<grpc_server_credentials> server_creds,
    RefCountedPtr<grpc_tls_credentials_options> options)
    : grpc_server_security_connector(GRPC_SSL_URL_SCHEME,
                                     std::move(server_creds)),
      options_(std::move(options)) {
  // Create a watcher.
  auto watcher_ptr = absl::make_unique<TlsServerCertificateWatcher>(this);
  certificate_watcher_ = watcher_ptr.get();
  // Register the watcher with the distributor.
  grpc_tls_certificate_distributor* distributor =
      options_->certificate_distributor();
  absl::optional<std::string> watched_root_cert_name;
  if (options_->watch_root_cert()) {
    watched_root_cert_name = options_->root_cert_name();
  }
  absl::optional<std::string> watched_identity_cert_name;
  if (options_->watch_identity_pair()) {
    watched_identity_cert_name = options_->identity_cert_name();
  }
  // Server side won't use default system roots at any time.
  distributor->WatchTlsCertificates(std::move(watcher_ptr),
                                    watched_root_cert_name,
                                    watched_identity_cert_name);
}

TlsServerSecurityConnector::~TlsServerSecurityConnector() {
  // Cancel all the watchers.
  grpc_tls_certificate_distributor* distributor =
      options_->certificate_distributor();
  distributor->CancelTlsCertificatesWatch(certificate_watcher_);
  if (server_handshaker_factory_ != nullptr) {
    tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
  }
}

void TlsServerSecurityConnector::add_handshakers(
    const grpc_channel_args* args, grpc_pollset_set* /*interested_parties*/,
    HandshakeManager* handshake_mgr) {
  MutexLock lock(&mu_);
  if (server_handshaker_factory_ != nullptr) {
    // Instantiate TSI handshaker.
    tsi_handshaker* tsi_hs = nullptr;
    tsi_result result = tsi_ssl_server_handshaker_factory_create_handshaker(
        server_handshaker_factory_, &tsi_hs);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
              tsi_result_to_string(result));
      return;
    }
    // Create handshakers.
    handshake_mgr->Add(SecurityHandshakerCreate(tsi_hs, this, args));
    return;
  }
  // TODO(ZhenLian): Implement the logic(delegation to
  // BlockOnInitialCredentialHandshaker) when certificates are not ready.
  gpr_log(GPR_ERROR, "%s not supported yet.",
          "Server BlockOnInitialCredentialHandshaker");
}

void TlsServerSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* /*ep*/,
    RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  grpc_error_handle error = grpc_ssl_check_alpn(&peer);
  *auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  tsi_peer_destruct(&peer);
  ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
}

int TlsServerSecurityConnector::cmp(
    const grpc_security_connector* other) const {
  return server_security_connector_cmp(
      static_cast<const grpc_server_security_connector*>(other));
}

void TlsServerSecurityConnector::TlsServerCertificateWatcher::
    OnCertificatesChanged(absl::optional<absl::string_view> root_certs,
                          absl::optional<PemKeyCertPairList> key_cert_pairs) {
  GPR_ASSERT(security_connector_ != nullptr);
  MutexLock lock(&security_connector_->mu_);
  if (root_certs.has_value()) {
    security_connector_->pem_root_certs_ = root_certs;
  }
  if (key_cert_pairs.has_value()) {
    security_connector_->pem_key_cert_pair_list_ = std::move(key_cert_pairs);
  }
  bool root_being_watched = security_connector_->options_->watch_root_cert();
  bool root_has_value = security_connector_->pem_root_certs_.has_value();
  bool identity_being_watched =
      security_connector_->options_->watch_identity_pair();
  bool identity_has_value =
      security_connector_->pem_key_cert_pair_list_.has_value();
  if ((root_being_watched && root_has_value && identity_being_watched &&
       identity_has_value) ||
      (root_being_watched && root_has_value && !identity_being_watched) ||
      (!root_being_watched && identity_being_watched && identity_has_value)) {
    if (security_connector_->UpdateHandshakerFactoryLocked() !=
        GRPC_SECURITY_OK) {
      gpr_log(GPR_ERROR, "Update handshaker factory failed.");
    }
  }
}

// TODO(ZhenLian): implement the logic to signal waiting handshakers once
// BlockOnInitialCredentialHandshaker is implemented.
void TlsServerSecurityConnector::TlsServerCertificateWatcher::OnError(
    grpc_error_handle root_cert_error, grpc_error_handle identity_cert_error) {
  if (root_cert_error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "TlsServerCertificateWatcher getting root_cert_error: %s",
            grpc_error_std_string(root_cert_error).c_str());
  }
  if (identity_cert_error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR,
            "TlsServerCertificateWatcher getting identity_cert_error: %s",
            grpc_error_std_string(identity_cert_error).c_str());
  }
  GRPC_ERROR_UNREF(root_cert_error);
  GRPC_ERROR_UNREF(identity_cert_error);
}

// TODO(ZhenLian): implement the logic to signal waiting handshakers once
// BlockOnInitialCredentialHandshaker is implemented.
grpc_security_status
TlsServerSecurityConnector::UpdateHandshakerFactoryLocked() {
  /* Free the server handshaker factory if exists. */
  if (server_handshaker_factory_ != nullptr) {
    tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
  }
  // The identity certs on the server side shouldn't be empty.
  GPR_ASSERT(pem_key_cert_pair_list_.has_value());
  GPR_ASSERT(!(*pem_key_cert_pair_list_).empty());
  std::string pem_root_certs;
  if (pem_root_certs_.has_value()) {
    // TODO(ZhenLian): update the underlying TSI layer to use C++ types like
    // std::string and absl::string_view to avoid making another copy here.
    pem_root_certs = std::string(*pem_root_certs_);
  }
  tsi_ssl_pem_key_cert_pair* pem_key_cert_pairs = nullptr;
  pem_key_cert_pairs = ConvertToTsiPemKeyCertPair(*pem_key_cert_pair_list_);
  size_t num_key_cert_pairs = (*pem_key_cert_pair_list_).size();
  grpc_security_status status = grpc_ssl_tsi_server_handshaker_factory_init(
      pem_key_cert_pairs, num_key_cert_pairs,
      pem_root_certs.empty() ? nullptr : pem_root_certs.c_str(),
      options_->cert_request_type(),
      grpc_get_tsi_tls_version(options_->min_tls_version()),
      grpc_get_tsi_tls_version(options_->max_tls_version()),
      &server_handshaker_factory_);
  /* Free memory. */
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(pem_key_cert_pairs,
                                          num_key_cert_pairs);
  return status;
}

namespace internal {

grpc_error_handle TlsCheckHostName(const char* peer_name,
                                   const tsi_peer* peer) {
  /* Check the peer name if specified. */
  if (peer_name != nullptr && !grpc_ssl_host_matches_name(peer, peer_name)) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Peer name ", peer_name, " is not in peer certificate")
            .c_str());
  }
  return GRPC_ERROR_NONE;
}

}  // namespace internal

}  // namespace grpc_core
