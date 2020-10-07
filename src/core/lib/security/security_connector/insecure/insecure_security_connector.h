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

#ifndef GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_INSECURE_INSECURE_SECURITY_CONNECTOR_H
#define GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_INSECURE_INSECURE_SECURITY_CONNECTOR_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/security_connector/security_connector.h"

namespace grpc_core {

/**
 * This method creates an insecure channel security connector.
 *
 * - channel_creds: channel credential instance.
 * - request_metadata_creds: credential object which will be sent with each
 *   request. This parameter can be nullptr.
 *
 * It returns nullptr on failure.
 */
RefCountedPtr<grpc_channel_security_connector>
InsecureChannelSecurityConnectorCreate(
    RefCountedPtr<grpc_channel_credentials> channel_creds,
    RefCountedPtr<grpc_call_credentials> request_metadata_creds);

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SECURITY_SECURITY_CONNECTOR_INSECURE_INSECURE_SECURITY_CONNECTOR_H \
        */