//
//
// Copyright 2023 gRPC authors.
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

#ifndef GRPC_GRPC_CRL_PROVIDER_H
#define GRPC_GRPC_CRL_PROVIDER_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

namespace grpc_core {
namespace experimental {

// Representation of a CRL
class Crl {};

// Representation of a Certificate
class Cert {};

// The base class for CRL Provider implementations.
class CrlProvider {
 public:
  CrlProvider() {}
  virtual Crl Crl(const Cert& cert) = 0;
};

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_GRPC_CRL_PROVIDER_H
