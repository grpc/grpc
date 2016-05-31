/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H

#include "src/core/lib/security/credentials/credentials.h"

typedef struct {
  grpc_call_credentials **creds_array;
  size_t num_creds;
} grpc_call_credentials_array;

const grpc_call_credentials_array *
grpc_composite_call_credentials_get_credentials(
    grpc_call_credentials *composite_creds);

/* Returns creds if creds is of the specified type or the inner creds of the
   specified type (if found), if the creds is of type COMPOSITE.
   If composite_creds is not NULL, *composite_creds will point to creds if of
   type COMPOSITE in case of success. */
grpc_call_credentials *grpc_credentials_contains_type(
    grpc_call_credentials *creds, const char *type,
    grpc_call_credentials **composite_creds);

/* -- Channel composite credentials. -- */

typedef struct {
  grpc_channel_credentials base;
  grpc_channel_credentials *inner_creds;
  grpc_call_credentials *call_creds;
} grpc_composite_channel_credentials;

/* -- Composite credentials. -- */

typedef struct {
  grpc_call_credentials base;
  grpc_call_credentials_array inner;
} grpc_composite_call_credentials;

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_COMPOSITE_COMPOSITE_CREDENTIALS_H \
          */
