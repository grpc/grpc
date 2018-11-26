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

#include "src/core/lib/security/credentials/credentials.h"

struct grpc_call_credentials_array {
  grpc_call_credentials** creds_array;
  size_t num_creds;
};

const grpc_call_credentials_array*
grpc_composite_call_credentials_get_credentials(
    grpc_call_credentials* composite_creds);

/* Returns creds if creds is of the specified type or the inner creds of the
   specified type (if found), if the creds is of type COMPOSITE.
   If composite_creds is not NULL, *composite_creds will point to creds if of
   type COMPOSITE in case of success. */
grpc_call_credentials* grpc_credentials_contains_type(
    grpc_call_credentials* creds, const char* type,
    grpc_call_credentials** composite_creds);

/* -- Composite channel credentials. -- */

class grpc_composite_channel_credentials : public grpc_channel_credentials {
 public:
  grpc_composite_channel_credentials(grpc_channel_credentials* channel_creds,
                                     grpc_call_credentials* call_creds)
      : grpc_channel_credentials(channel_creds->type()),
        inner_creds_(channel_creds->Ref()),
        call_creds_(call_creds->Ref()) {}

  ~grpc_composite_channel_credentials() override;

  grpc_channel_credentials* duplicate_without_call_credentials() override;

  grpc_security_status create_security_connector(
      grpc_call_credentials* call_creds, const char* target,
      const grpc_channel_args* args, grpc_channel_security_connector** sc,
      grpc_channel_args** new_args) override;

  const grpc_channel_credentials* inner_creds() const { return inner_creds_; }
  const grpc_call_credentials* call_creds() const { return call_creds_; }
  grpc_call_credentials* mutable_call_creds() { return call_creds_; }

 private:
  grpc_channel_credentials* inner_creds_;
  grpc_call_credentials* call_creds_;
};

/* -- Composite call credentials. -- */

class grpc_composite_call_credentials : public grpc_call_credentials {
 public:
  grpc_composite_call_credentials(grpc_call_credentials* creds1,
                                  grpc_call_credentials* creds2);
  ~grpc_composite_call_credentials() override;

  bool get_request_metadata(grpc_polling_entity* pollent,
                            grpc_auth_metadata_context context,
                            grpc_credentials_mdelem_array* md_array,
                            grpc_closure* on_request_metadata,
                            grpc_error** error) override;

  void cancel_get_request_metadata(grpc_credentials_mdelem_array* md_array,
                                   grpc_error* error) override;

  const grpc_call_credentials_array& inner() const { return inner_; }

 private:
  grpc_call_credentials_array inner_;
};

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H \
        */
