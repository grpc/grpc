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

#ifndef GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_TLS_CREDENTIALS_H
#define GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_TLS_CREDENTIALS_H

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/port_platform.h>

#include <memory>
#include <optional>
#include <utility>

#include "src/core/credentials/transport/security_connector.h"
#include "src/core/credentials/transport/tls/ssl_utils.h"
#include "src/core/credentials/transport/transport_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/tsi/ssl/key_logging/ssl_key_logging.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/unique_type_name.h"
#include "absl/base/thread_annotations.h"

class TlsCredentials final : public grpc_channel_credentials {
 public:
  explicit TlsCredentials(
      grpc_core::RefCountedPtr<grpc_tls_credentials_options> options);
  ~TlsCredentials() override;

  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
      const char* target_name, grpc_core::ChannelArgs* args) override;

  static grpc_core::UniqueTypeName Type();

  grpc_core::UniqueTypeName type() const override { return Type(); }

  grpc_tls_credentials_options* options() const { return options_.get(); }

  // Returns a refcounted tsi_ssl_client_handshaker_factory keyed by
  // (root_cert_info identity, identity_certs, ssl_session_cache). Other
  // factory inputs (TLS version bounds, verify_server_cert, CRL config, key
  // exchange groups, key logger) are intentionally NOT part of the cache key
  // because they are derivable from options_ and therefore invariant across
  // all connectors built from this credential. If a future change introduces
  // a per-connector override for any of them, it must be added to the key
  // (or the cache scope narrowed). cached_key_logger_ exists only to DCHECK
  // this invariant on cache hits.
  //
  // Thread-safe. Releases the lock during factory construction so unrelated
  // callers are not serialized on a slow build. Lock ordering: callers that
  // hold a TlsChannelSecurityConnector mutex acquire factory_cache_mu_ while
  // holding it; the credential never calls back into a connector while
  // holding factory_cache_mu_, so no inversion is possible.
  //
  // Returns (GRPC_SECURITY_OK, +1-ref'd factory) on success. On failure
  // returns the underlying status and a null factory; the cache is left
  // unchanged so concurrent waiters retry serially.
  std::pair<grpc_security_status, tsi_ssl_client_handshaker_factory*>
  GetOrCreateCachedClientHandshakerFactory(
      const std::shared_ptr<tsi::RootCertInfo>& root_cert_info,
      const std::optional<grpc_core::PemKeyCertPairList>& identity_certs,
      tsi_ssl_session_cache* ssl_session_cache,
      tsi::TlsSessionKeyLoggerCache::TlsSessionKeyLogger* key_logger)
      ABSL_LOCKS_EXCLUDED(factory_cache_mu_);

  bool HasCachedClientHandshakerFactoryForTesting()
      ABSL_LOCKS_EXCLUDED(factory_cache_mu_);

 private:
  int cmp_impl(const grpc_channel_credentials* other) const override;

  bool CacheMatchesLocked(
      const std::shared_ptr<tsi::RootCertInfo>& root_cert_info,
      const std::optional<grpc_core::PemKeyCertPairList>& identity_certs,
      tsi_ssl_session_cache* ssl_session_cache) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(factory_cache_mu_);

  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options_;

  grpc_core::Mutex factory_cache_mu_;
  grpc_core::CondVar factory_cache_cv_;
  bool factory_creation_in_progress_ ABSL_GUARDED_BY(factory_cache_mu_) = false;
  // The credential holds exactly one ref while non-null. Callers receive a
  // separately-incremented ref.
  tsi_ssl_client_handshaker_factory* cached_factory_
      ABSL_GUARDED_BY(factory_cache_mu_) = nullptr;
  std::shared_ptr<tsi::RootCertInfo> cached_root_cert_info_
      ABSL_GUARDED_BY(factory_cache_mu_);
  std::optional<grpc_core::PemKeyCertPairList> cached_identity_certs_
      ABSL_GUARDED_BY(factory_cache_mu_);
  tsi_ssl_session_cache* cached_session_cache_
      ABSL_GUARDED_BY(factory_cache_mu_) = nullptr;
  // Tripwire only -- not part of the cache key. See comment on
  // GetOrCreateCachedClientHandshakerFactory.
  tsi::TlsSessionKeyLoggerCache::TlsSessionKeyLogger* cached_key_logger_
      ABSL_GUARDED_BY(factory_cache_mu_) = nullptr;
};

class TlsServerCredentials final : public grpc_server_credentials {
 public:
  explicit TlsServerCredentials(
      grpc_core::RefCountedPtr<grpc_tls_credentials_options> options);
  ~TlsServerCredentials() override;

  grpc_core::RefCountedPtr<grpc_server_security_connector>
  create_security_connector(const grpc_core::ChannelArgs& /* args */) override;

  static grpc_core::UniqueTypeName Type();

  grpc_core::UniqueTypeName type() const override { return Type(); }

  grpc_tls_credentials_options* options() const { return options_.get(); }

 private:
  grpc_core::RefCountedPtr<grpc_tls_credentials_options> options_;
};

#endif  // GRPC_SRC_CORE_CREDENTIALS_TRANSPORT_TLS_TLS_CREDENTIALS_H
