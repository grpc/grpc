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

#include "src/core/lib/security/security_connector/tls/spiffe_security_connector.h"

#include <stdbool.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/security/credentials/ssl/ssl_credentials.h"
#include "src/core/lib/security/credentials/tls/spiffe_credentials.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"
#include "src/core/lib/security/transport/security_handshaker.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/tsi/transport_security.h"

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

/** -- Util function to populate SPIFFE server/channel credentials. -- */
grpc_core::RefCountedPtr<grpc_tls_key_materials_config>
PopulateSpiffeCredentials(const grpc_tls_credentials_options& options) {
  GPR_ASSERT(options.credential_reload_config() != nullptr ||
             options.key_materials_config() != nullptr);
  grpc_core::RefCountedPtr<grpc_tls_key_materials_config> key_materials_config;
  /* Use credential reload config to fetch credentials. */
  if (options.credential_reload_config() != nullptr) {
    grpc_tls_credential_reload_arg* arg =
        grpc_core::New<grpc_tls_credential_reload_arg>();
    key_materials_config = grpc_tls_key_materials_config_create()->Ref();
    arg->key_materials_config = key_materials_config.get();
    int result = options.credential_reload_config()->Schedule(arg);
    if (result) {
      /* Do not support async credential reload. */
      gpr_log(GPR_ERROR, "Async credential reload is unsupported now.");
    } else {
      grpc_ssl_certificate_config_reload_status status = arg->status;
      if (status == GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED) {
        gpr_log(GPR_DEBUG, "Credential does not change after reload.");
      } else if (status == GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL) {
        gpr_log(GPR_ERROR, "Credential reload failed with an error: %s",
                arg->error_details);
      }
    }
    gpr_free((void*)arg->error_details);
    grpc_core::Delete(arg);
    /* Use existing key materials config. */
  } else {
    key_materials_config = options.key_materials_config()->Ref();
  }
  return key_materials_config;
}

}  // namespace

SpiffeChannelSecurityConnector::SpiffeChannelSecurityConnector(
    grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
    grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const char* target_name, const char* overridden_target_name)
    : grpc_channel_security_connector(GRPC_SSL_URL_SCHEME,
                                      std::move(channel_creds),
                                      std::move(request_metadata_creds)),
      overridden_target_name_(overridden_target_name == nullptr
                                  ? nullptr
                                  : gpr_strdup(overridden_target_name)) {
  check_arg_ = ServerAuthorizationCheckArgCreate(this);
  char* port;
  gpr_split_host_port(target_name, &target_name_, &port);
  gpr_free(port);
}

SpiffeChannelSecurityConnector::~SpiffeChannelSecurityConnector() {
  if (target_name_ != nullptr) {
    gpr_free(target_name_);
  }
  if (overridden_target_name_ != nullptr) {
    gpr_free(overridden_target_name_);
  }
  if (client_handshaker_factory_ != nullptr) {
    tsi_ssl_client_handshaker_factory_unref(client_handshaker_factory_);
  }
  ServerAuthorizationCheckArgDestroy(check_arg_);
}

void SpiffeChannelSecurityConnector::add_handshakers(
    grpc_pollset_set* interested_parties,
    grpc_core::HandshakeManager* handshake_mgr) {
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

void SpiffeChannelSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* ep,
    grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  const char* target_name = overridden_target_name_ != nullptr
                                ? overridden_target_name_
                                : target_name_;
  grpc_error* error = grpc_ssl_check_alpn(&peer);
  if (error != GRPC_ERROR_NONE) {
    GRPC_CLOSURE_SCHED(on_peer_checked, error);
    tsi_peer_destruct(&peer);
    return;
  }
  *auth_context = grpc_ssl_peer_to_auth_context(&peer);
  const SpiffeCredentials* creds =
      static_cast<const SpiffeCredentials*>(channel_creds());
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
      char* peer_pem = static_cast<char*>(gpr_malloc(p->value.length + 1));
      memcpy(peer_pem, p->value.data, p->value.length);
      peer_pem[p->value.length] = '\0';
      GPR_ASSERT(check_arg_ != nullptr);
      check_arg_->peer_cert = check_arg_->peer_cert == nullptr
                                  ? gpr_strdup(peer_pem)
                                  : check_arg_->peer_cert;
      check_arg_->target_name = check_arg_->target_name == nullptr
                                    ? gpr_strdup(target_name)
                                    : check_arg_->target_name;
      on_peer_checked_ = on_peer_checked;
      gpr_free(peer_pem);
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
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
  tsi_peer_destruct(&peer);
}

int SpiffeChannelSecurityConnector::cmp(
    const grpc_security_connector* other_sc) const {
  auto* other =
      reinterpret_cast<const SpiffeChannelSecurityConnector*>(other_sc);
  int c = channel_security_connector_cmp(other);
  if (c != 0) {
    return c;
  }
  return grpc_ssl_cmp_target_name(target_name_, other->target_name_,
                                  overridden_target_name_,
                                  other->overridden_target_name_);
}

bool SpiffeChannelSecurityConnector::check_call_host(
    const char* host, grpc_auth_context* auth_context,
    grpc_closure* on_call_host_checked, grpc_error** error) {
  return grpc_ssl_check_call_host(host, target_name_, overridden_target_name_,
                                  auth_context, on_call_host_checked, error);
}

void SpiffeChannelSecurityConnector::cancel_check_call_host(
    grpc_closure* on_call_host_checked, grpc_error* error) {
  GRPC_ERROR_UNREF(error);
}

grpc_core::RefCountedPtr<grpc_channel_security_connector>
SpiffeChannelSecurityConnector::CreateSpiffeChannelSecurityConnector(
    grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
    grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
    const char* target_name, const char* overridden_target_name,
    tsi_ssl_session_cache* ssl_session_cache) {
  if (channel_creds == nullptr) {
    gpr_log(GPR_ERROR,
            "channel_creds is nullptr in "
            "SpiffeChannelSecurityConnectorCreate()");
    return nullptr;
  }
  if (target_name == nullptr) {
    gpr_log(GPR_ERROR,
            "target_name is nullptr in "
            "SpiffeChannelSecurityConnectorCreate()");
    return nullptr;
  }
  grpc_core::RefCountedPtr<SpiffeChannelSecurityConnector> c =
      grpc_core::MakeRefCounted<SpiffeChannelSecurityConnector>(
          std::move(channel_creds), std::move(request_metadata_creds),
          target_name, overridden_target_name);
  if (c->InitializeHandshakerFactory(ssl_session_cache) != GRPC_SECURITY_OK) {
    return nullptr;
  }
  return c;
}

grpc_security_status
SpiffeChannelSecurityConnector::InitializeHandshakerFactory(
    tsi_ssl_session_cache* ssl_session_cache) {
  const SpiffeCredentials* creds =
      static_cast<const SpiffeCredentials*>(channel_creds());
  auto key_materials_config = PopulateSpiffeCredentials(creds->options());
  if (key_materials_config->pem_key_cert_pair_list().empty()) {
    key_materials_config->Unref();
    return GRPC_SECURITY_ERROR;
  }
  tsi_ssl_pem_key_cert_pair* pem_key_cert_pair = ConvertToTsiPemKeyCertPair(
      key_materials_config->pem_key_cert_pair_list());
  grpc_security_status status = grpc_ssl_tsi_client_handshaker_factory_init(
      pem_key_cert_pair, key_materials_config->pem_root_certs(),
      ssl_session_cache, &client_handshaker_factory_);
  // Free memory.
  key_materials_config->Unref();
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(pem_key_cert_pair, 1);
  return status;
}

void SpiffeChannelSecurityConnector::ServerAuthorizationCheckDone(
    grpc_tls_server_authorization_check_arg* arg) {
  GPR_ASSERT(arg != nullptr);
  grpc_core::ExecCtx exec_ctx;
  grpc_error* error = ProcessServerAuthorizationCheckResult(arg);
  SpiffeChannelSecurityConnector* connector =
      static_cast<SpiffeChannelSecurityConnector*>(arg->cb_user_data);
  GRPC_CLOSURE_SCHED(connector->on_peer_checked_, error);
}

grpc_error*
SpiffeChannelSecurityConnector::ProcessServerAuthorizationCheckResult(
    grpc_tls_server_authorization_check_arg* arg) {
  grpc_error* error = GRPC_ERROR_NONE;
  char* msg = nullptr;
  /* Server authorization check is cancelled by caller. */
  if (arg->status == GRPC_STATUS_CANCELLED) {
    gpr_asprintf(&msg,
                 "Server authorization check is cancelled by the caller with "
                 "error: %s",
                 arg->error_details);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
  } else if (arg->status == GRPC_STATUS_OK) {
    /* Server authorization check completed successfully but returned check
     * failure. */
    if (!arg->success) {
      gpr_asprintf(&msg, "Server authorization check failed with error: %s",
                   arg->error_details);
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    }
    /* Server authorization check did not complete correctly. */
  } else {
    gpr_asprintf(
        &msg,
        "Server authorization check did not finish correctly with error: %s",
        arg->error_details);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
  }
  gpr_free(msg);
  return error;
}

grpc_tls_server_authorization_check_arg*
SpiffeChannelSecurityConnector::ServerAuthorizationCheckArgCreate(
    void* user_data) {
  grpc_tls_server_authorization_check_arg* arg =
      grpc_core::New<grpc_tls_server_authorization_check_arg>();
  arg->cb = ServerAuthorizationCheckDone;
  arg->cb_user_data = user_data;
  arg->status = GRPC_STATUS_OK;
  return arg;
}

void SpiffeChannelSecurityConnector::ServerAuthorizationCheckArgDestroy(
    grpc_tls_server_authorization_check_arg* arg) {
  if (arg == nullptr) {
    return;
  }
  gpr_free((void*)arg->target_name);
  gpr_free((void*)arg->peer_cert);
  gpr_free((void*)arg->error_details);
  grpc_core::Delete(arg);
}

SpiffeServerSecurityConnector::SpiffeServerSecurityConnector(
    grpc_core::RefCountedPtr<grpc_server_credentials> server_creds)
    : grpc_server_security_connector(GRPC_SSL_URL_SCHEME,
                                     std::move(server_creds)) {}

SpiffeServerSecurityConnector::~SpiffeServerSecurityConnector() {
  if (server_handshaker_factory_ != nullptr) {
    tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
  }
}

void SpiffeServerSecurityConnector::add_handshakers(
    grpc_pollset_set* interested_parties,
    grpc_core::HandshakeManager* handshake_mgr) {
  /* Create a TLS SPIFFE TSI handshaker for server. */
  RefreshServerHandshakerFactory();
  tsi_handshaker* tsi_hs = nullptr;
  tsi_result result = tsi_ssl_server_handshaker_factory_create_handshaker(
      server_handshaker_factory_, &tsi_hs);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Handshaker creation failed with error %s.",
            tsi_result_to_string(result));
    return;
  }
  handshake_mgr->Add(grpc_core::SecurityHandshakerCreate(tsi_hs, this));
}

void SpiffeServerSecurityConnector::check_peer(
    tsi_peer peer, grpc_endpoint* ep,
    grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
    grpc_closure* on_peer_checked) {
  grpc_error* error = grpc_ssl_check_alpn(&peer);
  *auth_context = grpc_ssl_peer_to_auth_context(&peer);
  tsi_peer_destruct(&peer);
  GRPC_CLOSURE_SCHED(on_peer_checked, error);
}

int SpiffeServerSecurityConnector::cmp(
    const grpc_security_connector* other) const {
  return server_security_connector_cmp(
      static_cast<const grpc_server_security_connector*>(other));
}

grpc_core::RefCountedPtr<grpc_server_security_connector>
SpiffeServerSecurityConnector::CreateSpiffeServerSecurityConnector(
    grpc_core::RefCountedPtr<grpc_server_credentials> server_creds) {
  if (server_creds == nullptr) {
    gpr_log(GPR_ERROR,
            "server_creds is nullptr in "
            "SpiffeServerSecurityConnectorCreate()");
    return nullptr;
  }
  grpc_core::RefCountedPtr<SpiffeServerSecurityConnector> c =
      grpc_core::MakeRefCounted<SpiffeServerSecurityConnector>(
          std::move(server_creds));
  if (c->RefreshServerHandshakerFactory() != GRPC_SECURITY_OK) {
    return nullptr;
  }
  return c;
}

grpc_security_status
SpiffeServerSecurityConnector::RefreshServerHandshakerFactory() {
  const SpiffeServerCredentials* creds =
      static_cast<const SpiffeServerCredentials*>(server_creds());
  auto key_materials_config = PopulateSpiffeCredentials(creds->options());
  /* Credential reload does NOT take effect and we need to keep using
   * the existing handshaker factory. */
  if (key_materials_config->pem_key_cert_pair_list().empty()) {
    key_materials_config->Unref();
    return GRPC_SECURITY_ERROR;
  }
  /* Credential reload takes effect and we need to free the existing
   * handshaker library. */
  if (server_handshaker_factory_) {
    tsi_ssl_server_handshaker_factory_unref(server_handshaker_factory_);
  }
  tsi_ssl_pem_key_cert_pair* pem_key_cert_pairs = ConvertToTsiPemKeyCertPair(
      key_materials_config->pem_key_cert_pair_list());
  size_t num_key_cert_pairs =
      key_materials_config->pem_key_cert_pair_list().size();
  grpc_security_status status = grpc_ssl_tsi_server_handshaker_factory_init(
      pem_key_cert_pairs, num_key_cert_pairs,
      key_materials_config->pem_root_certs(),
      creds->options().cert_request_type(), &server_handshaker_factory_);
  // Free memory.
  key_materials_config->Unref();
  grpc_tsi_ssl_pem_key_cert_pairs_destroy(pem_key_cert_pairs,
                                          num_key_cert_pairs);
  return status;
}
