/*
 *
 * Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_FILE_WATCHER_CERTIFICATE_PROVIDER_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_FILE_WATCHER_CERTIFICATE_PROVIDER_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc_security.h>

#include "absl/container/inlined_vector.h"

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/lib/security/security_connector/ssl_utils.h"

#include <string.h>

#include <chrono>
#include <thread>
#include <functional>

namespace grpc_core {

class FileWatcherCertificateProvider : public grpc_tls_certificate_provider {
 public:
  FileWatcherCertificateProvider(const char* private_key_file_name, const char* identity_certificate_file_name, const char* root_certificate_file_name, unsigned int root_interval,
                                 unsigned int identity_interval);

  RefCountedPtr<grpc_tls_certificate_distributor> distributor()
      const override {
    return distributor_;
  }

  void Shutdown() {
    grpc_core::MutexLock lock(&mu_);
    is_shutdown_ = true;
  }

 private:
  RefCountedPtr<grpc_tls_certificate_distributor> distributor_;
  grpc_core::Mutex mu_;
  bool is_shutdown_ = false;
};

}  // namespace grpc_core


#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_TLS_GRPC_TLS_FILE_WATCHER_CERTIFICATE_PROVIDER_H \
        */
