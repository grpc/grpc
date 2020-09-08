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

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/pollset_set.h"

// TODO(yashkt): After https://github.com/grpc/grpc/pull/23572, remove this
// forward declaration and include the header for the distributor instead.
struct grpc_tls_certificate_distributor;

// Interface for a grpc_tls_certificate_provider that handles the process to
// fetch credentials and validation contexts. Implementations are free to rely
// on local or remote sources to fetch the latest secrets, and free to share any
// state among different instances as they deem fit.
//
// On creation, grpc_tls_certificate_provider creates a
// grpc_tls_certificate_distributor object. When the credentials and validation
// contexts become valid or changed, a grpc_tls_certificate_provider should
// notify its distributor so as to propagate the update to the watchers.
struct grpc_tls_certificate_provider
    : public grpc_core::RefCounted<grpc_tls_certificate_provider> {
 public:
  grpc_tls_certificate_provider()
      : interested_parties_(grpc_pollset_set_create()) {}

  virtual ~grpc_tls_certificate_provider() {
    grpc_pollset_set_destroy(interested_parties_);
  }

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

  virtual grpc_core::RefCountedPtr<grpc_tls_certificate_distributor>
  distributor() const = 0;

 private:
  grpc_pollset_set* interested_parties_;
};

#endif  // GRPC_CORE_LIB_SECURITY_CERTIFICATE_PROVIDER_H
