//
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
//

#ifndef GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_H
#define GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"

namespace grpc_core {

// Interface for a CertificateProvider that handles the process to fetch
// credentials and validation contexts. Implementations are free to rely on
// local or remote sources to fetch the latest secrets, and free to share any
// state among different instances as they deem fit.
//
// On creation, CertificateProvider creates a grpc_tls_certificate_distributor
// object. When the credentials and validation contexts become valid or changed,
// a CertificateProvider should notify its distributor so as to propagate the
// update to the watchers. This distributor also owns an OrphanablePtr to this
// CertificateProvider, thereby orphaning the CertificateProvider when it is no
// longer needed, serving as an indication to start shutdown.
class CertificateProvider : public InternallyRefCounted<CertificateProvider> {
 public:
  CertificateProvider()
      : distributor_(new grpc_tls_certificate_distributor(this)),
        interested_parties_(grpc_pollset_set_create()) {}

  virtual ~CertificateProvider() {
    grpc_pollset_set_destroy(interested_parties_);
  }

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

  grpc_tls_certificate_distributor* distributor() const {
    return distributor_.get();
  }

 private:
  // We don't need to hold a ref to \a distributor. It should not be used after
  // distributor_ has shutdown (notified by Orphan()).
  grpc_tls_certificate_distributor* distributor_;
  grpc_pollset_set* interested_parties_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_H
