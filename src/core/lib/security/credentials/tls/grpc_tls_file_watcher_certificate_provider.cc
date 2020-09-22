//
// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/tls/grpc_tls_file_watcher_certificate_provider.h"

#include "src/core/lib/iomgr/load_file.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

namespace grpc_core {


constexpr const unsigned int kDefaultRootCertInterval = 1000; // 1 s
constexpr const unsigned int kDefaultIdentityCertInterval = 1000; // 1 s

FileWatcherCertificateProvider::FileWatcherCertificateProvider(const char* private_key_file_name, const char* identity_certificate_file_name, const char* root_certificate_file_name, unsigned int root_interval,
                                 unsigned int identity_interval)
                                 : distributor_(
          MakeRefCounted<grpc_tls_certificate_distributor>()) {
  // ...register watch status callback with distributor...
  std::thread root_thread([=]() {
      while(true) {
          {
            grpc_core::MutexLock lock(&mu_);
            if(is_shutdown_) return;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(root_interval));
          {
            grpc_core::MutexLock lock(&mu_);
            if(is_shutdown_) return;
            grpc_slice root_slice;
            GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                               grpc_load_file(root_certificate_file_name, 1, &root_slice)));
            std::string root_cert = std::string(reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(root_slice)), GRPC_SLICE_LENGTH(root_slice));
            // FileWatcherCertificateProvider uses the default cert name "" for root certificates.
            distributor_->SetKeyMaterials("", std::move(root_cert), absl::nullopt);
            grpc_slice_unref(root_slice);
          }
      }
  });
  root_thread.detach();
  std::thread identity_thread([=]() {
      while(true) {
          {
            grpc_core::MutexLock lock(&mu_);
            if(is_shutdown_) return;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(identity_interval));
          {
            grpc_core::MutexLock lock(&mu_);
            if(is_shutdown_) return;
            grpc_slice cert_slice, key_slice;
            GPR_ASSERT(GRPC_LOG_IF_ERROR(
                "load_file", grpc_load_file(identity_certificate_file_name, 1, &cert_slice)));
            GPR_ASSERT(GRPC_LOG_IF_ERROR("load_file",
                                         grpc_load_file(private_key_file_name, 1, &key_slice)));
            grpc_tls_certificate_distributor::PemKeyCertPairList identity_pairs;
            grpc_ssl_pem_key_cert_pair* ssl_pair =
            static_cast<grpc_ssl_pem_key_cert_pair*>(
                gpr_malloc(sizeof(grpc_ssl_pem_key_cert_pair)));
            ssl_pair->private_key = gpr_strdup(reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(key_slice)));
            ssl_pair->cert_chain = gpr_strdup(reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(cert_slice)));
            identity_pairs.emplace_back(ssl_pair);
            // FileWatcherCertificateProvider uses the default cert name "" for identity certificates.
            distributor_->SetKeyMaterials("", absl::nullopt, std::move(identity_pairs));
            grpc_slice_unref(cert_slice);
            grpc_slice_unref(key_slice);
          }
      }
  });
  identity_thread.detach();
}

}  // namespace grpc_core

/** -- Wrapper APIs declared in grpc_security.h -- **/

grpc_tls_certificate_provider* grpc_tls_certificate_provider_file_watcher_create(const char* private_key_file_name, const char* identity_certificate_file_name, const char* root_certificate_file_name) {
  return new grpc_core::FileWatcherCertificateProvider(private_key_file_name, identity_certificate_file_name, root_certificate_file_name, grpc_core::kDefaultRootCertInterval, grpc_core::kDefaultIdentityCertInterval);
}

