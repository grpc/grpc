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

#include "src/core/lib/security/security_connector/tls/tls_security_connector.h"

#include <string.h>

#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_verifier.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/tsi/ssl_transport_security.h"

namespace grpc_core {

namespace {

char* CopyCoreString(char* src, size_t length) {
  char* target = static_cast<char*>(gpr_malloc(length + 1));
  memcpy(target, src, length);
  target[length] = '\0';
  return target;
}

void PendingVerifierRequestInit(
    const char* target_name, tsi_peer peer,
    grpc_tls_custom_verification_check_request* request) {
  GPR_ASSERT(request != nullptr);
  // The verifier holds a ref to the security connector, so it's fine to
  // directly point this to the name cached in the security connector.
  request->target_name = target_name;
  // TODO(ZhenLian): avoid the copy when the underlying core implementation used
  // the null-terminating string.
  bool has_common_name = false;
  bool has_peer_cert = false;
  bool has_peer_cert_full_chain = false;
  bool has_verified_root_cert_subject = false;
  std::vector<char*> uri_names;
  std::vector<char*> dns_names;
  std::vector<char*> email_names;
  std::vector<char*> ip_names;
  for (size_t i = 0; i < peer.property_count; ++i) {
    const tsi_peer_property* prop = &peer.properties[i];
    if (prop->name == nullptr) continue;
    if (strcmp(prop->name, TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY) == 0) {
      request->peer_info.common_name =
          CopyCoreString(prop->value.data, prop->value.length);
      has_common_name = true;
    } else if (strcmp(prop->name, TSI_X509_PEM_CERT_PROPERTY) == 0) {
      request->peer_info.peer_cert =
          CopyCoreString(prop->value.data, prop->value.length);
      has_peer_cert = true;
    } else if (strcmp(prop->name, TSI_X509_PEM_CERT_CHAIN_PROPERTY) == 0) {
      request->peer_info.peer_cert_full_chain =
          CopyCoreString(prop->value.data, prop->value.length);
      has_peer_cert_full_chain = true;
    } else if (strcmp(prop->name, TSI_X509_URI_PEER_PROPERTY) == 0) {
      char* uri = CopyCoreString(prop->value.data, prop->value.length);
      uri_names.emplace_back(uri);
    } else if (strcmp(prop->name, TSI_X509_DNS_PEER_PROPERTY) == 0) {
      char* dns = CopyCoreString(prop->value.data, prop->value.length);
      dns_names.emplace_back(dns);
    } else if (strcmp(prop->name, TSI_X509_EMAIL_PEER_PROPERTY) == 0) {
      char* email = CopyCoreString(prop->value.data, prop->value.length);
      email_names.emplace_back(email);
    } else if (strcmp(prop->name, TSI_X509_IP_PEER_PROPERTY) == 0) {
      char* ip = CopyCoreString(prop->value.data, prop->value.length);
      ip_names.emplace_back(ip);
    } else if (strcmp(prop->name,
                      TSI_X509_VERIFIED_ROOT_CERT_SUBECT_PEER_PROPERTY) == 0) {
      request->peer_info.verified_root_cert_subject =
          CopyCoreString(prop->value.data, prop->value.length);
      has_verified_root_cert_subject = true;
    }
  }
  if (!has_common_name) {
    request->peer_info.common_name = nullptr;
  }
  if (!has_peer_cert) {
    request->peer_info.peer_cert = nullptr;
  }
  if (!has_peer_cert_full_chain) {
    request->peer_info.peer_cert_full_chain = nullptr;
  }
  if (!has_verified_root_cert_subject) {
    request->peer_info.verified_root_cert_subject = nullptr;
  }
  request->peer_info.san_names.uri_names_size = uri_names.size();
  if (!uri_names.empty()) {
    request->peer_info.san_names.uri_names =
        new char*[request->peer_info.san_names.uri_names_size];
    for (size_t i = 0; i < request->peer_info.san_names.uri_names_size; ++i) {
      // We directly point the char* string stored in vector to the |request|.
      // That string will be released when the |request| is destroyed.
      request->peer_info.san_names.uri_names[i] = uri_names[i];
    }
  } else {
    request->peer_info.san_names.uri_names = nullptr;
  }
  request->peer_info.san_names.dns_names_size = dns_names.size();
  if (!dns_names.empty()) {
    request->peer_info.san_names.dns_names =
        new char*[request->peer_info.san_names.dns_names_size];
    for (size_t i = 0; i < request->peer_info.san_names.dns_names_size; ++i) {
      // We directly point the char* string stored in vector to the |request|.
      // That string will be released when the |request| is destroyed.
      request->peer_info.san_names.dns_names[i] = dns_names[i];
    }
  } else {
    request->peer_info.san_names.dns_names = nullptr;
  }
  request->peer_info.san_names.email_names_size = email_names.size();
  if (!email_names.empty()) {
    request->peer_info.san_names.email_names =
        new char*[request->peer_info.san_names.email_names_size];
    for (size_t i = 0; i < request->peer_info.san_names.email_names_size; ++i) {
      // We directly point the char* string stored in vector to the |request|.
      // That string will be released when the |request| is destroyed.
      request->peer_info.san_names.email_names[i] = email_names[i];
    }
  } else {
    request->peer_info.san_names.email_names = nullptr;
  }
  request->peer_info.san_names.ip_names_size = ip_names.size();
  if (!ip_names.empty()) {
    request->peer_info.san_names.ip_names =
        new char*[request->peer_info.san_names.ip_names_size];
    for (size_t i = 0; i < request->peer_info.san_names.ip_names_size; ++i) {
      // We directly point the char* string stored in vector to the |request|.
      // That string will be released when the |request| is destroyed.
      request->peer_info.san_names.ip_names[i] = ip_names[i];
    }
  } else {
    request->peer_info.san_names.ip_names = nullptr;
  }
}

void PendingVerifierRequestDestroy(
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
  if (request->peer_info.san_names.dns_names_size > 0) {
    for (size_t i = 0; i < request->peer_info.san_names.dns_names_size; ++i) {
      gpr_free(request->peer_info.san_names.dns_names[i]);
    }
    delete[] request->peer_info.san_names.dns_names;
  }
  if (request->peer_info.san_names.email_names_size > 0) {
    for (size_t i = 0; i < request->peer_info.san_names.email_names_size; ++i) {
      gpr_free(request->peer_info.san_names.email_names[i]);
    }
    delete[] request->peer_info.san_names.email_names;
  }
  if (request->peer_info.san_names.ip_names_size > 0) {
    for (size_t i = 0; i < request->peer_info.san_names.ip_names_size; ++i) {
      gpr_free(request->peer_info.san_names.ip_names[i]);
    }
    delete[] request->peer_info.san_names.ip_names;
  }
  if (request->peer_info.peer_cert != nullptr) {
    gpr_free(const_cast<char*>(request->peer_info.peer_cert));
  }
  if (request->peer_info.peer_cert_full_chain != nullptr) {
    gpr_free(const_cast<char*>(request->peer_info.peer_cert_full_chain));
  }
  if (request->peer_info.verified_root_cert_subject != nullptr) {
    gpr_free(const_cast<char*>(request->peer_info.verified_root_cert_subject));
  }
}

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
  const std::string& tls_session_key_log_file_path =
      options_->tls_session_key_log_file_path();
  if (!tls_session_key_log_file_path.empty()) {
    tls_session_key_logger_ =
        tsi::TlsSessionKeyLoggerCache::Get(tls_session_key_log_file_path);
  }
  if (ssl_session_cache_ != nullptr) {
    tsi_ssl_session_cache_ref(ssl_session_cache_);
  }
  absl::string_view host;
  absl::string_view port;
  SplitHostPort(target_name, &host, &port);
  target_name_ = std::string(host);
  // Create a watcher.
  auto watcher_ptr = std::make_unique<TlsChannelCertificateWatcher>(this);
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
    const ChannelArgs& args, grpc_pollset_set* /*interested_parties*/,
    HandshakeManager* handshake_mgr) {
  MutexLock lock(&mu_);
  tsi_handshaker* tsi_hs = nullptr;
  if (client_handshaker_factory_ != nullptr) {
    // Instantiate TSI handshaker.
    tsi_result result = tsi_ssl_client_handshaker_factory_create_handshaker(
        client_handshaker_factory_,
        overridden_target_name_.empty() ? target_name_.c_str()
                                        : overridden_target_name_.c_str(),
        /*network_bio_buf_size=*/0,
        /*ssl_bio_buf_size=*/0, &tsi_hs);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
              tsi_result_to_string(result));
    }
  }
  // If tsi_hs is null, this will add a failing handshaker.
  handshake_mgr->Add(SecurityHandshakerCreate(tsi_hs, this, args));
}

void TlsChannelSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* /*ep*/, const ChannelArgs& /*args*/,
    RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  const char* target_name = overridden_target_name_.empty()
                                ? target_name_.c_str()
                                : overridden_target_name_.c_str();
  grpc_error_handle error = grpc_ssl_check_alpn(&peer);
  if (!error.ok()) {
    ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
    tsi_peer_destruct(&peer);
    return;
  }
  *auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  GPR_ASSERT(options_->certificate_verifier() != nullptr);
  auto* pending_request = new ChannelPendingVerifierRequest(
      RefAsSubclass<TlsChannelSecurityConnector>(), on_peer_checked, peer,
      target_name);
  {
    MutexLock lock(&verifier_request_map_mu_);
    pending_verifier_requests_.emplace(on_peer_checked, pending_request);
  }
  pending_request->Start();
}

void TlsChannelSecurityConnector::cancel_check_peer(
    grpc_closure* on_peer_checked, grpc_error_handle /*error*/) {
  auto* verifier = options_->certificate_verifier();
  if (verifier != nullptr) {
    grpc_tls_custom_verification_check_request* pending_verifier_request =
        nullptr;
    {
      MutexLock lock(&verifier_request_map_mu_);
      auto it = pending_verifier_requests_.find(on_peer_checked);
      if (it != pending_verifier_requests_.end()) {
        pending_verifier_request = it->second->request();
      } else {
        gpr_log(GPR_INFO,
                "TlsChannelSecurityConnector::cancel_check_peer: no "
                "corresponding pending request found");
      }
    }
    if (pending_verifier_request != nullptr) {
      verifier->Cancel(pending_verifier_request);
    }
  }
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
  return 0;
}

ArenaPromise<absl::Status> TlsChannelSecurityConnector::CheckCallHost(
    absl::string_view host, grpc_auth_context* auth_context) {
  if (options_->check_call_host()) {
    return Immediate(SslCheckCallHost(host, target_name_.c_str(),
                                      overridden_target_name_.c_str(),
                                      auth_context));
  }
  return ImmediateOkStatus();
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
  if (!root_cert_error.ok()) {
    gpr_log(GPR_ERROR,
            "TlsChannelCertificateWatcher getting root_cert_error: %s",
            StatusToString(root_cert_error).c_str());
  }
  if (!identity_cert_error.ok()) {
    gpr_log(GPR_ERROR,
            "TlsChannelCertificateWatcher getting identity_cert_error: %s",
            StatusToString(identity_cert_error).c_str());
  }
}

TlsChannelSecurityConnector::ChannelPendingVerifierRequest::
    ChannelPendingVerifierRequest(
        RefCountedPtr<TlsChannelSecurityConnector> security_connector,
        grpc_closure* on_peer_checked, tsi_peer peer, const char* target_name)
    : security_connector_(std::move(security_connector)),
      on_peer_checked_(on_peer_checked) {
  PendingVerifierRequestInit(target_name, peer, &request_);
  tsi_peer_destruct(&peer);
}

TlsChannelSecurityConnector::ChannelPendingVerifierRequest::
    ~ChannelPendingVerifierRequest() {
  PendingVerifierRequestDestroy(&request_);
}

void TlsChannelSecurityConnector::ChannelPendingVerifierRequest::Start() {
  absl::Status sync_status;
  grpc_tls_certificate_verifier* verifier =
      security_connector_->options_->certificate_verifier();
  bool is_done = verifier->Verify(
      &request_,
      absl::bind_front(&ChannelPendingVerifierRequest::OnVerifyDone, this,
                       true),
      &sync_status);
  if (is_done) {
    OnVerifyDone(false, sync_status);
  }
}

void TlsChannelSecurityConnector::ChannelPendingVerifierRequest::OnVerifyDone(
    bool run_callback_inline, absl::Status status) {
  {
    MutexLock lock(&security_connector_->verifier_request_map_mu_);
    security_connector_->pending_verifier_requests_.erase(on_peer_checked_);
  }
  grpc_error_handle error;
  if (!status.ok()) {
    error = GRPC_ERROR_CREATE(
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
  // Free the client handshaker factory if exists.
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
      tls_session_key_logger_.get(), options_->crl_directory().c_str(),
      options_->crl_provider(), &client_handshaker_factory_);
  // Free memory.
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
  const std::string& tls_session_key_log_file_path =
      options_->tls_session_key_log_file_path();
  if (!tls_session_key_log_file_path.empty()) {
    tls_session_key_logger_ =
        tsi::TlsSessionKeyLoggerCache::Get(tls_session_key_log_file_path);
  }
  // Create a watcher.
  auto watcher_ptr = std::make_unique<TlsServerCertificateWatcher>(this);
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
    const ChannelArgs& args, grpc_pollset_set* /*interested_parties*/,
    HandshakeManager* handshake_mgr) {
  MutexLock lock(&mu_);
  tsi_handshaker* tsi_hs = nullptr;
  if (server_handshaker_factory_ != nullptr) {
    // Instantiate TSI handshaker.
    tsi_result result = tsi_ssl_server_handshaker_factory_create_handshaker(
        server_handshaker_factory_, /*network_bio_buf_size=*/0,
        /*ssl_bio_buf_size=*/0, &tsi_hs);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
              tsi_result_to_string(result));
    }
  }
  // If tsi_hs is null, this will add a failing handshaker.
  handshake_mgr->Add(SecurityHandshakerCreate(tsi_hs, this, args));
}

void TlsServerSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* /*ep*/, const ChannelArgs& /*args*/,
    RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  grpc_error_handle error = grpc_ssl_check_alpn(&peer);
  if (!error.ok()) {
    ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
    tsi_peer_destruct(&peer);
    return;
  }
  *auth_context =
      grpc_ssl_peer_to_auth_context(&peer, GRPC_TLS_TRANSPORT_SECURITY_TYPE);
  if (options_->certificate_verifier() != nullptr) {
    auto* pending_request = new ServerPendingVerifierRequest(
        RefAsSubclass<TlsServerSecurityConnector>(), on_peer_checked, peer);
    {
      MutexLock lock(&verifier_request_map_mu_);
      pending_verifier_requests_.emplace(on_peer_checked, pending_request);
    }
    pending_request->Start();
  } else {
    tsi_peer_destruct(&peer);
    ExecCtx::Run(DEBUG_LOCATION, on_peer_checked, error);
  }
}

void TlsServerSecurityConnector::cancel_check_peer(
    grpc_closure* on_peer_checked, grpc_error_handle /*error*/) {
  auto* verifier = options_->certificate_verifier();
  if (verifier != nullptr) {
    grpc_tls_custom_verification_check_request* pending_verifier_request =
        nullptr;
    {
      MutexLock lock(&verifier_request_map_mu_);
      auto it = pending_verifier_requests_.find(on_peer_checked);
      if (it != pending_verifier_requests_.end()) {
        pending_verifier_request = it->second->request();
      } else {
        gpr_log(GPR_INFO,
                "TlsServerSecurityConnector::cancel_check_peer: no "
                "corresponding pending request found");
      }
    }
    if (pending_verifier_request != nullptr) {
      verifier->Cancel(pending_verifier_request);
    }
  }
}

int TlsServerSecurityConnector::cmp(
    const grpc_security_connector* other_sc) const {
  auto* other = reinterpret_cast<const TlsServerSecurityConnector*>(other_sc);
  int c = server_security_connector_cmp(other);
  if (c != 0) return c;
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
  if (!root_cert_error.ok()) {
    gpr_log(GPR_ERROR,
            "TlsServerCertificateWatcher getting root_cert_error: %s",
            StatusToString(root_cert_error).c_str());
  }
  if (!identity_cert_error.ok()) {
    gpr_log(GPR_ERROR,
            "TlsServerCertificateWatcher getting identity_cert_error: %s",
            StatusToString(identity_cert_error).c_str());
  }
}

TlsServerSecurityConnector::ServerPendingVerifierRequest::
    ServerPendingVerifierRequest(
        RefCountedPtr<TlsServerSecurityConnector> security_connector,
        grpc_closure* on_peer_checked, tsi_peer peer)
    : security_connector_(std::move(security_connector)),
      on_peer_checked_(on_peer_checked) {
  PendingVerifierRequestInit(nullptr, peer, &request_);
  tsi_peer_destruct(&peer);
}

TlsServerSecurityConnector::ServerPendingVerifierRequest::
    ~ServerPendingVerifierRequest() {
  PendingVerifierRequestDestroy(&request_);
}

void TlsServerSecurityConnector::ServerPendingVerifierRequest::Start() {
  absl::Status sync_status;
  grpc_tls_certificate_verifier* verifier =
      security_connector_->options_->certificate_verifier();
  bool is_done = verifier->Verify(
      &request_,
      absl::bind_front(&ServerPendingVerifierRequest::OnVerifyDone, this, true),
      &sync_status);
  if (is_done) {
    OnVerifyDone(false, sync_status);
  }
}

void TlsServerSecurityConnector::ServerPendingVerifierRequest::OnVerifyDone(
    bool run_callback_inline, absl::Status status) {
  {
    MutexLock lock(&security_connector_->verifier_request_map_mu_);
    security_connector_->pending_verifier_requests_.erase(on_peer_checked_);
  }
  grpc_error_handle error;
  if (!status.ok()) {
    error = GRPC_ERROR_CREATE(
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
  // Free the server handshaker factory if exists.
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
      tls_session_key_logger_.get(), options_->crl_directory().c_str(),
      options_->send_client_ca_list(), options_->crl_provider(),
      &server_handshaker_factory_);
  // Free memory.
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(pem_key_cert_pairs,
                                          num_key_cert_pairs);
  return status;
}

}  // namespace grpc_core
