/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/security/credentials/credentials.h"

// TODO(soheil): Replace this with InlinedVector once #16032 is resolved.
class grpc_call_credentials_array {
 public:
  grpc_call_credentials_array() = default;
  grpc_call_credentials_array(const grpc_call_credentials_array& that);

  ~grpc_call_credentials_array();

  void reserve(size_t capacity);

  // Must reserve before pushing any data.
  void push_back(grpc_core::RefCountedPtr<grpc_call_credentials> cred) {
    GPR_DEBUG_ASSERT(capacity_ > num_creds_);
    new (&creds_array_[num_creds_++])
        grpc_core::RefCountedPtr<grpc_call_credentials>(std::move(cred));
  }

  const grpc_core::RefCountedPtr<grpc_call_credentials>& get(size_t i) const {
    GPR_DEBUG_ASSERT(i < num_creds_);
    return creds_array_[i];
  }
  grpc_core::RefCountedPtr<grpc_call_credentials>& get_mutable(size_t i) {
    GPR_DEBUG_ASSERT(i < num_creds_);
    return creds_array_[i];
  }

  size_t size() const { return num_creds_; }

 private:
  grpc_core::RefCountedPtr<grpc_call_credentials>* creds_array_ = nullptr;
  size_t num_creds_ = 0;
  size_t capacity_ = 0;
};

/* -- Composite channel credentials. -- */

class grpc_composite_channel_credentials : public grpc_channel_credentials {
 public:
  grpc_composite_channel_credentials(
      grpc_core::RefCountedPtr<grpc_channel_credentials> channel_creds,
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds)
      : grpc_channel_credentials(channel_creds->type()),
        inner_creds_(std::move(channel_creds)),
        call_creds_(std::move(call_creds)) {}

  ~grpc_composite_channel_credentials() override = default;

  grpc_core::RefCountedPtr<grpc_channel_credentials>
  duplicate_without_call_credentials() override {
    return inner_creds_;
  }

  grpc_core::RefCountedPtr<grpc_channel_security_connector>
  create_security_connector(
      grpc_core::RefCountedPtr<grpc_call_credentials> call_creds,
      const char* target, const grpc_channel_args* args,
      grpc_channel_args** new_args) override;

  const grpc_channel_credentials* inner_creds() const {
    return inner_creds_.get();
  }
  const grpc_call_credentials* call_creds() const { return call_creds_.get(); }
  grpc_call_credentials* mutable_call_creds() { return call_creds_.get(); }

 private:
  grpc_core::RefCountedPtr<grpc_channel_credentials> inner_creds_;
  grpc_core::RefCountedPtr<grpc_call_credentials> call_creds_;
};

/* -- Composite call credentials. -- */

class grpc_composite_call_credentials : public grpc_call_credentials {
 public:
  grpc_composite_call_credentials(
      grpc_core::RefCountedPtr<grpc_call_credentials> creds1,
      grpc_core::RefCountedPtr<grpc_call_credentials> creds2);
  ~grpc_composite_call_credentials() override = default;

  bool get_request_metadata(grpc_polling_entity* pollent,
                            grpc_auth_metadata_context context,
                            grpc_credentials_mdelem_array* md_array,
                            grpc_closure* on_request_metadata,
                            grpc_error** error) override;

  void cancel_get_request_metadata(grpc_credentials_mdelem_array* md_array,
                                   grpc_error* error) override;

  const grpc_call_credentials_array& inner() const { return inner_; }

 private:
  void push_to_inner(grpc_core::RefCountedPtr<grpc_call_credentials> creds,
                     bool is_composite);

  grpc_call_credentials_array inner_;
};

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H \
        */
