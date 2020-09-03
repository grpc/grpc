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

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/certificate_provider_registry.h"

namespace grpc_core {

namespace {

class RegistryState {
public:
	void RegisterCertificateProviderFactory(std::unique_ptr<CertificateProviderFactory> factory) {
		gpr_lo
	}

private:
	std::unordered_map<std::string /* plugin name */,
                     std::unique_ptr<CertificateProviderFactory>>
      registry_;
}

} // namespace

} // namespace grpc_core
