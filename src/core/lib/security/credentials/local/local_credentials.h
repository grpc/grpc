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

#ifndef GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_LOCAL_LOCAL_CREDENTIALS_H
#define GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_LOCAL_LOCAL_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/grpc_security_constants.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/unique_type_name.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"

// Main class for grpc local channel credential.
class grpc_local_credentials final : public grpc_channel_credentials {
 public:
  explicit grpc_local_credentials(grpc_local_connect_type connect_type);
  ~grpc_local_credentials() override = default;

  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> request_metadata_creds,
      const char* target_name, grpc_core::ChannelArgs* args) override;

  grpc_core::UniqueTypeName type() const override;

  grpc_local_connect_type connect_type() const { return connect_type_; }

 private:
  int cmp_impl(const grpc_channel_credentials* other) const override {
    // TODO(yashykt): Check if we can do something better here
    return grpc_core::QsortCompare(
        static_cast<const grpc_channel_credentials*>(this), other);
  }

  grpc_local_connect_type connect_type_;
};

// Main class for grpc local server credential.
class grpc_local_server_credentials final : public grpc_server_credentials {
 public:
  explicit grpc_local_server_credentials(grpc_local_connect_type connect_type);
  ~grpc_local_server_credentials() override = default;

  grpc_core::RefCountedPtr<grpc_server_security_connector>
  create_security_connector(const grpc_core::ChannelArgs& /* args */) override;

  grpc_core::UniqueTypeName type() const override;

  grpc_local_connect_type connect_type() const { return connect_type_; }

 private:
  grpc_local_connect_type connect_type_;
};

#endif  // GRPC_SRC_CORE_LIB_SECURITY_CREDENTIALS_LOCAL_LOCAL_CREDENTIALS_H
