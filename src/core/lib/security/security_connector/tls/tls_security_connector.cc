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
#include <grpc/grpc_security_constants.h>
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

char* CopyCoreString(char* src, size_t length) {
  char* target = static_cast<char*>(gpr_malloc(length + 1));
  memcpy(target, src, length);
  target[length] = '\0';
  return target;
}

}  // namespace

PendingVerifierRequest::PendingVerifierRequest(grpc_closure* on_peer_checked,
                                               tsi_peer peer)
    : on_peer_checked_(on_peer_checked), peer_(peer) {
  PendingVerifierRequestInit(&request_);
  // Parse tsi_peer and feed in the values in the check request.
  // We will make a copy of each field and destroy them when request_ is
  // destroyed.
  // TODO(ZhenLian): avoid the copy when the underlying core implementation used
  // the null-terminating string.
  std::vector<char*> uri_names;
  std::vector<char*> dns_names;
  for (size_t i = 0; i < peer.property_count; ++i) {
    const tsi_peer_property* prop = &peer.properties[i];
    if (prop->name == nullptr) continue;
    if (strcmp(prop->name, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY) == 0) {
      request_.peer_info.common_name =
          CopyCoreString(prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_PEM_CERT_PROPERTY) == 0) {
      request_.peer_info.peer_cert =
          CopyCoreString(prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_PEM_CERT_CHAIN_PROPERTY) == 0) {
      request_.peer_info.peer_cert_full_chain =
          CopyCoreString(prop->value.data, prop->value.length);
    } else if (strcmp(prop->name, TSI_X509_URI_PEER_PROPERTY) == 0) {
      char* uri = CopyCoreString(prop->value.data, prop->value.length);
      uri_names.emplace_back(uri);
    } else if (strcmp(prop->name,
                      TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) == 0) {
      // TODO(ZhenLian): The logic here is wrong.
      // We are passing all SAN names as DNS names, because the DNS names are
      // not plumbed. Once it is plumbed, this should be changed.
      char* dns = CopyCoreString(prop->value.data, prop->value.length);
      dns_names.emplace_back(dns);
    } else {
      // Not supported fields.
      // TODO(ZhenLian): populate IP Address and other fields here as well.
    }
  }
  request_.peer_info.san_names.uri_names_size = uri_names.size();
  if (!uri_names.empty()) {
    request_.peer_info.san_names.uri_names =
        new char*[request_.peer_info.san_names.uri_names_size];
    for (size_t i = 0; i < request_.peer_info.san_names.uri_names_size; ++i) {
      // We directly point the char* string stored in vector to the |request|.
      // That string will be released when the |request| is destroyed.
      request_.peer_info.san_names.uri_names[i] = uri_names[i];
    }
  }
  request_.peer_info.san_names.dns_names_size = dns_names.size();
  if (!dns_names.empty()) {
    request_.peer_info.san_names.dns_names =
        new char*[request_.peer_info.san_names.dns_names_size];
    for (size_t i = 0; i < request_.peer_info.san_names.dns_names_size; ++i) {
      // We directly point the char* string stored in vector to the |request|.
      // That string will be released when the |request| is destroyed.
      request_.peer_info.san_names.dns_names[i] = dns_names[i];
    }
  }
}

PendingVerifierRequest::~PendingVerifierRequest() {
  tsi_peer_destruct(&peer_);
  PendingVerifierRequestDestroy(&request_);
}

void PendingVerifierRequest::PendingVerifierRequestInit(
    grpc_tls_custom_verification_check_request* request) {
  GPR_ASSERT(request != nullptr);
  request->target_name = nullptr;
  request->peer_info.common_name = nullptr;
  request->peer_info.san_names.uri_names = nullptr;
  request->peer_info.san_names.uri_names_size = 0;
  request->peer_info.san_names.ip_names = nullptr;
  request->peer_info.san_names.ip_names_size = 0;
  request->peer_info.san_names.dns_names = nullptr;
  request->peer_info.san_names.dns_names_size = 0;
  request->peer_info.peer_cert = nullptr;
  request->peer_info.peer_cert_full_chain = nullptr;
  request->status = GRPC_STATUS_CANCELLED;
  request->error_details = nullptr;
}

void PendingVerifierRequest::PendingVerifierRequestDestroy(
    grpc_tls_custom_verification_check_request* request) {
  GPR_ASSERT(request != nullptr);
  if (request->peer_info.common_name != nullptr) {
    gpr_free(const_cast<char*>(request->peer_info.common_name));
  }
  if (request->peer_info.san_names.uri_names_size > 0) {
    for (size_t i = 0; i < request->peer_info.san_names.uri_names_size; ++i) {
      gpr_free(request->peer_info.san_names.uri_names[i]);
    }
    delete[] request->peer_info.san_names.uri_names;
  }
  if (request->peer_info.san_names.ip_names_size > 0) {
    for (size_t i = 0; i < request->peer_info.san_names.ip_names_size; ++i) {
      gpr_free(request->peer_info.san_names.ip_names[i]);
    }
    delete[] request->peer_info.san_names.ip_names;
  }
  if (request->peer_info.san_names.dns_names_size > 0) {
    for (size_t i = 0; i < request->peer_info.san_names.dns_names_size; ++i) {
      gpr_free(request->peer_info.san_names.dns_names[i]);
    }
    delete[] request->peer_info.san_names.dns_names;
  }
  if (request->peer_info.peer_cert != nullptr) {
    gpr_free(const_cast<char*>(request->peer_info.peer_cert));
  }
  if (request->peer_info.peer_cert_full_chain != nullptr) {
    gpr_free(const_cast<char*>(request->peer_info.peer_cert_full_chain));
  }
  if (request->error_details != nullptr) {
    gpr_free(const_cast<char*>(request->error_details));
  }
}

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
}

void TlsChannelSecurityConnector::add_handshakers(
    const grpc_channel_args* args, grpc_pollset_set* /*interested_parties*/,
    HandshakeManager* handshake_mgr) {
  MutexLock lock(&mu_);
  tsi_handshaker* tsi_hs = nullptr;
  if (client_handshaker_factory_ != nullptr) {
    // Instantiate TSI handshaker.
    tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
        client_handshaker_factory_,
        overridden_target_name_.empty() ? target_name_.c_str()
                                        : overridden_target_name_.c_str(),
        &tsi_hs);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
              tsi_result_to_string(result));
    }
  }
  // If tsi_hs is null, this will add a failing handshaker.
  handshake_mgr->Add(SecurityHandshakerCreate(tsi_hs, this, args));
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
  GPR_ASSERT(options_->certificate_verifier() != nullptr);
  auto* pending_request = new ChannelPendingVerifierRequest(
      Ref(), on_peer_checked, peer, target_name);
  {
    MutexLock lock(&verifier_request_map_mu_);
    pending_verifier_requests_.emplace(on_peer_checked, pending_request);
  }
  pending_request->Start();
}

int TlsChannelSecurityConnector::cmp(
    const grpc_security_connector* other_sc) const {
  auto* other = reinterpret_cast<const TlsChannelSecurityConnector*>(other_sc);
  int c = channel_security_connector_cmp(other);
  if (c != 0) return c;
  c = grpc_ssl_cmp_target_name(
      target_name_.c_str(), other->target_name_.c_str(),
      overridden_target_name_.c_str(), other->overridden_target_name_.c_str());
  if (c != 0) return c;
  if (pem_root_certs_ != other->pem_root_certs_ ||
      pem_key_cert_pair_list_ != other->pem_key_cert_pair_list_)
    return 1;
  if (certificate_watcher_ != other->certificate_watcher_ ||
      client_handshaker_factory_ != other->client_handshaker_factory_ ||
      ssl_session_cache_ != other->ssl_session_cache_)
    return 1;
  return 0;
}

bool TlsChannelSecurityConnector::check_call_host(
    absl::string_view host, grpc_auth_context* auth_context,
    grpc_closure* /*on_call_host_checked*/, grpc_error_handle* error) {
  // Question: shall we apply the verifier logic here as well?
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

void TlsChannelSecurityConnector::ChannelPendingVerifierRequest::OnVerifyDone(
    bool run_callback_inline, absl::Status status) {
  {
    MutexLock lock(&security_connector_->verifier_request_map_mu_);
    security_connector_->pending_verifier_requests_.erase(on_peer_checked_);
  }
  grpc_error* error = GRPC_ERROR_NONE;
  if (!status.ok()) {
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Custom verification check failed with error: ",
                     status.ToString())
            .c_str());
  }
  if (run_callback_inline) {
    Closure::Run(DEBUG_LOCATION, on_peer_checked_, error);
  } else {
    ExecCtx::Run(DEBUG_LOCATION, on_peer_checked_, error);
  }
  delete this;
}

// TODO(ZhenLian): implement the logic to signal waiting handshakers once
// BlockOnInitialCredentialHandshaker is implemented.
grpc_security_status
TlsChannelSecurityConnector::UpdateHandshakerFactoryLocked() {
  bool skip_server_certificate_verification = !options_->verify_server_cert();
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
  tsi_handshaker* tsi_hs = nullptr;
  if (server_handshaker_factory_ != nullptr) {
    // Instantiate TSI handshaker.
    tsi_result result = tsi_ssl_server_handshaker_factory_create_handshaker(
        server_handshaker_factory_, &tsi_hs);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
              tsi_result_to_string(result));
    }
  }
  // If tsi_hs is null, this will add a failing handshaker.
  handshake_mgr->Add(SecurityHandshakerCreate(tsi_hs, this, args));
}

void TlsServerSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* /*ep*/,
    RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  grpc_error_handle error = grpc_ssl_check_alpn(&peer);
  if (error != GRPC_ERROR_NONE) {
    ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
    tsi_peer_destruct(&peer);
    return;
  }
  *auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  auto* pending_request =
      new ServerPendingVerifierRequest(Ref(), on_peer_checked, peer);
  {
    MutexLock lock(&verifier_request_map_mu_);
    pending_verifier_requests_.emplace(on_peer_checked, pending_request);
  }
  pending_request->Start();
}

int TlsServerSecurityConnector::cmp(
    const grpc_security_connector* other_sc) const {
  auto* other = reinterpret_cast<const TlsServerSecurityConnector*>(other_sc);
  int c = server_security_connector_cmp(other);
  if (c != 0) return c;
  if (pem_root_certs_ != other->pem_root_certs_ ||
      pem_key_cert_pair_list_ != other->pem_key_cert_pair_list_)
    return 1;
  if (certificate_watcher_ != other->certificate_watcher_ ||
      server_handshaker_factory_ != other->server_handshaker_factory_)
    return 1;
  return 0;
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

void TlsServerSecurityConnector::ServerPendingVerifierRequest::OnVerifyDone(
    bool run_callback_inline, absl::Status status) {
  {
    MutexLock lock(&security_connector_->verifier_request_map_mu_);
    security_connector_->pending_verifier_requests_.erase(on_peer_checked_);
  }
  grpc_error* error = GRPC_ERROR_NONE;
  if (!status.ok()) {
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Custom verification check failed with error: ",
                     status.ToString())
            .c_str());
  }
  if (run_callback_inline) {
    Closure::Run(DEBUG_LOCATION, on_peer_checked_, error);
  } else {
    ExecCtx::Run(DEBUG_LOCATION, on_peer_checked_, error);
  }
  delete this;
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

<<<<<<< HEAD
namespace internal {

grpc_error_handle TlsCheckHostName(const char* peer_name,
                                   const tsi_peer* peer) {
  /* Check the peer name if specified. */
  if (peer_name != nullptr && !grpc_ssl_host_matches_name(peer, peer_name)) {
    return GRPC_ERROR_CREATE_FROM_CPP_STRING(
        absl::StrCat("Peer name ", peer_name, " is not in peer certificate"));
  }
  return GRPC_ERROR_NONE;
}

}  // namespace internal

=======
>>>>>>> 55cbaf32a7 (custom verification proposed C core changes)
}  // namespace grpc_core
