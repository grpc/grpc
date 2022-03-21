/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/fake/fake_credentials.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/security/security_connector/fake/fake_security_connector.h"

/* -- Fake transport security credentials. -- */

namespace {
class grpc_fake_channel_credentials final : public grpc_channel_credentials {
 public:
  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
      const char* target, const grpc_channel_args* args,
      grpc_channel_args** /*new_args*/) override {
    return grpc_fake_channel_security_connector_create(
        this->Ref(), std::move(call_creds), target, args);
  }

  const char* type() const override { return "Fake"; }

 private:
  int cmp_impl(const grpc_channel_credentials* other) const override {
    // TODO(yashykt): Check if we can do something better here
    return grpc_core::QsortCompare(
        static_cast<const grpc_channel_credentials*>(this), other);
  }
};

class grpc_fake_server_credentials final : public grpc_server_credentials {
 public:
  grpc_core::RefCountedPtr<grpc_server_security_connector>
  create_security_connector(const grpc_channel_args* /*args*/) override {
    return grpc_fake_server_security_connector_create(this->Ref());
  }

  const char* type() const override { return "Fake"; }
};
}  // namespace

grpc_channel_credentials* grpc_fake_transport_security_credentials_create() {
  return new grpc_fake_channel_credentials();
}

grpc_server_credentials*
grpc_fake_transport_security_server_credentials_create() {
  return new grpc_fake_server_credentials();
}

grpc_arg grpc_fake_transport_expected_targets_arg(char* expected_targets) {
  return grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS),
      expected_targets);
}

const char* grpc_fake_transport_get_expected_targets(
    const grpc_channel_args* args) {
  const grpc_arg* expected_target_arg =
      grpc_channel_args_find(args, GRPC_ARG_FAKE_SECURITY_EXPECTED_TARGETS);
  return grpc_channel_arg_get_string(expected_target_arg);
}

/* -- Metadata-only test credentials. -- */

grpc_core::ArenaPromise<absl::StatusOr<grpc_core::ClientMetadataHandle>>
grpc_md_only_test_credentials::GetRequestMetadata(
    grpc_core::ClientMetadataHandle initial_metadata,
    const grpc_call_credentials::GetRequestMetadataArgs*) {
  initial_metadata->Append(
      key_.as_string_view(), value_.Ref(),
      [](absl::string_view, const grpc_core::Slice&) { abort(); });
  return grpc_core::Immediate(std::move(initial_metadata));
}

const char* grpc_md_only_test_credentials::Type() { return "MdOnlyTest"; }

grpc_call_credentials* grpc_md_only_test_credentials_create(
    const char* md_key, const char* md_value) {
  return new grpc_md_only_test_credentials(md_key, md_value);
}
