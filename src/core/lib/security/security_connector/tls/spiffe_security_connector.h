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

#ifndef GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_SPIFFE_SECURITY_CONNECTOR_H
#define GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_SPIFFE_SECURITY_CONNECTOR_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

#define GRPC_TLS_SPIFFE_TRANSPORT_SECURITY_TYPE "spiffe"

// Spiffe channel security connector.
class SpiffeChannelSecurityConnector final
    : public grpc_channel_security_connector {
 public:
  // static factory method to create a SPIFFE channel security connector.
  static grpc_core::RefCountedPtr<grpc_channel_security_connector>
  CreateSpiffeChannelSecurityConnector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target_name, const char* overridden_target_name,
      tsi_ssl_session_cache* ssl_session_cache);

  SpiffeChannelSecurityConnector(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target_name, const char* overridden_target_name);
  ~SpiffeChannelSecurityConnector() override;

  void add_handshakers(grpc_pollset_set* interested_parties,
                       grpc_core::HandshakeManager* handshake_mgr) override;

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override;

  int cmp(const grpc_security_connector* other_sc) const override;

  bool check_call_host(const char* host, grpc_auth_context* auth_context,
                       grpc_closure* on_call_host_checked,
                       grpc_error** error) override;

  void cancel_check_call_host(grpc_closure* on_call_host_checked,
                              grpc_error* error) override;

 private:
  // Initialize SSL TSI client handshaker factory.
  grpc_security_status InitializeHandshakerFactory(
      tsi_ssl_session_cache* ssl_session_cache);

  // gRPC-provided callback executed by application, which servers to bring the
  // control back to gRPC core.
  static void ServerAuthorizationCheckDone(
      grpc_tls_server_authorization_check_arg* arg);

  // A util function to process server authorization check result.
  static grpc_error* ProcessServerAuthorizationCheckResult(
      grpc_tls_server_authorization_check_arg* arg);

  // A util function to create a server authorization check arg instance.
  static grpc_tls_server_authorization_check_arg*
  ServerAuthorizationCheckArgCreate(void* user_data);

  // A util function to destroy a server authorization check arg instance.
  static void ServerAuthorizationCheckArgDestroy(
      grpc_tls_server_authorization_check_arg* arg);

  grpc_closure* on_peer_checked_;
  char* target_name_;
  char* overridden_target_name_;
  tsi_ssl_client_handshaker_factory* client_handshaker_factory_ = nullptr;
  grpc_tls_server_authorization_check_arg* check_arg_;
};

// Spiffe server security connector.
class SpiffeServerSecurityConnector final
    : public grpc_server_security_connector {
 public:
  // static factory method to create a SPIFFE server security connector.
  static grpc_core::RefCountedPtr<grpc_server_security_connector>
  CreateSpiffeServerSecurityConnector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds);

  explicit SpiffeServerSecurityConnector(
      grpc_core::RefCountedPtr<grpc_server_credentials> server_creds);
  ~SpiffeServerSecurityConnector() override;

  void add_handshakers(grpc_pollset_set* interested_parties,
                       grpc_core::HandshakeManager* handshake_mgr) override;

  void check_peer(tsi_peer peer, grpc_endpoint* ep,
                  grpc_core::RefCountedPtr<grpc_auth_context>* auth_context,
                  grpc_closure* on_peer_checked) override;

  int cmp(const grpc_security_connector* other) const override;

 private:
  // A util function to refresh SSL TSI server handshaker factory with a valid
  // credential.
  grpc_security_status RefreshServerHandshakerFactory();
  tsi_ssl_server_handshaker_factory* server_handshaker_factory_ = nullptr;
};

#endif /* GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_TLS_SPIFFE_SECURITY_CONNECTOR_H \
        */
