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

typedef struct {
  grpc_call_credentials** creds_array;
  size_t num_creds;
} grpc_call_credentials_array;

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

typedef struct {
  grpc_channel_credentials base;
  grpc_channel_credentials* inner_creds;
  grpc_call_credentials* call_creds;
} grpc_composite_channel_credentials;

/* -- Composite call credentials. -- */

typedef struct {
  grpc_call_credentials base;
  grpc_call_credentials_array inner;
} grpc_composite_call_credentials;

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H \
        */
