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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_JWT_JWT_VERIFIER_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_JWT_JWT_VERIFIER_H

#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/json/json.h"

#include <grpc/slice.h>
#include <grpc/support/time.h>

/* --- Constants. --- */

#define GRPC_OPENID_CONFIG_URL_SUFFIX "/.well-known/openid-configuration"
#define GRPC_GOOGLE_SERVICE_ACCOUNTS_EMAIL_DOMAIN "gserviceaccount.com"
#define GRPC_GOOGLE_SERVICE_ACCOUNTS_KEY_URL_PREFIX \
  "www.googleapis.com/robot/v1/metadata/x509"

/* --- grpc_jwt_verifier_status. --- */

typedef enum {
  GRPC_JWT_VERIFIER_OK = 0,
  GRPC_JWT_VERIFIER_BAD_SIGNATURE,
  GRPC_JWT_VERIFIER_BAD_FORMAT,
  GRPC_JWT_VERIFIER_BAD_AUDIENCE,
  GRPC_JWT_VERIFIER_KEY_RETRIEVAL_ERROR,
  GRPC_JWT_VERIFIER_TIME_CONSTRAINT_FAILURE,
  GRPC_JWT_VERIFIER_BAD_SUBJECT,
  GRPC_JWT_VERIFIER_GENERIC_ERROR
} grpc_jwt_verifier_status;

const char *grpc_jwt_verifier_status_to_string(grpc_jwt_verifier_status status);

/* --- grpc_jwt_claims. --- */

typedef struct grpc_jwt_claims grpc_jwt_claims;

void grpc_jwt_claims_destroy(grpc_exec_ctx *exec_ctx, grpc_jwt_claims *claims);

/* Returns the whole JSON tree of the claims. */
const grpc_json *grpc_jwt_claims_json(const grpc_jwt_claims *claims);

/* Access to registered claims in https://tools.ietf.org/html/rfc7519#page-9 */
const char *grpc_jwt_claims_subject(const grpc_jwt_claims *claims);
const char *grpc_jwt_claims_issuer(const grpc_jwt_claims *claims);
const char *grpc_jwt_claims_id(const grpc_jwt_claims *claims);
const char *grpc_jwt_claims_audience(const grpc_jwt_claims *claims);
gpr_timespec grpc_jwt_claims_issued_at(const grpc_jwt_claims *claims);
gpr_timespec grpc_jwt_claims_expires_at(const grpc_jwt_claims *claims);
gpr_timespec grpc_jwt_claims_not_before(const grpc_jwt_claims *claims);

/* --- grpc_jwt_verifier. --- */

typedef struct grpc_jwt_verifier grpc_jwt_verifier;

typedef struct {
  /* The email domain is the part after the @ sign. */
  const char *email_domain;

  /* The key url prefix will be used to get the public key from the issuer:
     https://<key_url_prefix>/<issuer_email>
     Therefore the key_url_prefix must NOT contain https://. */
  const char *key_url_prefix;
} grpc_jwt_verifier_email_domain_key_url_mapping;

/* Globals to control the verifier. Not thread-safe. */
extern gpr_timespec grpc_jwt_verifier_clock_skew;
extern gpr_timespec grpc_jwt_verifier_max_delay;

/* The verifier can be created with some custom mappings to help with key
   discovery in the case where the issuer is an email address.
   mappings can be NULL in which case num_mappings MUST be 0.
   A verifier object has one built-in mapping (unless overridden):
   GRPC_GOOGLE_SERVICE_ACCOUNTS_EMAIL_DOMAIN ->
   GRPC_GOOGLE_SERVICE_ACCOUNTS_KEY_URL_PREFIX.*/
grpc_jwt_verifier *grpc_jwt_verifier_create(
    const grpc_jwt_verifier_email_domain_key_url_mapping *mappings,
    size_t num_mappings);

/*The verifier must not be destroyed if there are still outstanding callbacks.*/
void grpc_jwt_verifier_destroy(grpc_jwt_verifier *verifier);

/* User provided callback that will be called when the verification of the JWT
   is done (maybe in another thread).
   It is the responsibility of the callee to call grpc_jwt_claims_destroy on
   the claims. */
typedef void (*grpc_jwt_verification_done_cb)(grpc_exec_ctx *exec_ctx,
                                              void *user_data,
                                              grpc_jwt_verifier_status status,
                                              grpc_jwt_claims *claims);

/* Verifies for the JWT for the given expected audience. */
void grpc_jwt_verifier_verify(grpc_exec_ctx *exec_ctx,
                              grpc_jwt_verifier *verifier,
                              grpc_pollset *pollset, const char *jwt,
                              const char *audience,
                              grpc_jwt_verification_done_cb cb,
                              void *user_data);

/* --- TESTING ONLY exposed functions. --- */

grpc_jwt_claims *grpc_jwt_claims_from_json(grpc_exec_ctx *exec_ctx,
                                           grpc_json *json, grpc_slice buffer);
grpc_jwt_verifier_status grpc_jwt_claims_check(const grpc_jwt_claims *claims,
                                               const char *audience);
const char *grpc_jwt_issuer_email_domain(const char *issuer);

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_JWT_JWT_VERIFIER_H */
