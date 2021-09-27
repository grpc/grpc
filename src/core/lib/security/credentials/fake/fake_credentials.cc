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
#include "src/core/lib/security/security_connector/fake/fake_security_connector.h"

/* -- Fake transport security credentials. -- */

namespace {
class grpc_fake_channel_credentials final : public grpc_channel_credentials {
 public:
  grpc_fake_channel_credentials()
      : grpc_channel_credentials(
            GRPC_CHANNEL_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY) {}
  ~grpc_fake_channel_credentials() override = default;

  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
      const char* target, const grpc_channel_args* args,
      grpc_channel_args** /*new_args*/) override {
    return grpc_fake_channel_security_connector_create(
        this->Ref(), std::move(call_creds), target, args);
  }
};

class grpc_fake_server_credentials final : public grpc_server_credentials {
 public:
  grpc_fake_server_credentials()
      : grpc_server_credentials(
            GRPC_CHANNEL_CREDENTIALS_TYPE_FAKE_TRANSPORT_SECURITY) {}
  ~grpc_fake_server_credentials() override = default;

  grpc_core::RefCountedPtr<grpc_server_security_connector>
  create_security_connector(const grpc_channel_args* /*args*/) override {
    return grpc_fake_server_security_connector_create(this->Ref());
  }
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

bool grpc_md_only_test_credentials::get_request_metadata(
    grpc_polling_entity* /*pollent*/, grpc_auth_metadata_context /*context*/,
    grpc_credentials_mdelem_array* md_array, grpc_closure* on_request_metadata,
    grpc_error_handle* /*error*/) {
  grpc_credentials_mdelem_array_add(md_array, md_);
  if (is_async_) {
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_request_metadata,
                            GRPC_ERROR_NONE);
    return false;
  }
  return true;
}

void grpc_md_only_test_credentials::cancel_get_request_metadata(
    grpc_credentials_mdelem_array* /*md_array*/, grpc_error_handle error) {
  GRPC_ERROR_UNREF(error);
}

grpc_call_credentials* grpc_md_only_test_credentials_create(
    const char* md_key, const char* md_value, bool is_async) {
  return new grpc_md_only_test_credentials(md_key, md_value, is_async);
}
