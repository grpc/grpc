/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_GRPC_SECURITY_CONSTANTS_H
#define GRPC_GRPC_SECURITY_CONSTANTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define GRPC_TRANSPORT_SECURITY_TYPE_PROPERTY_NAME "transport_security_type"
#define GRPC_SSL_TRANSPORT_SECURITY_TYPE "ssl"

#define GRPC_X509_CN_PROPERTY_NAME "x509_common_name"
#define GRPC_X509_SAN_PROPERTY_NAME "x509_subject_alternative_name"
#define GRPC_X509_PEM_CERT_PROPERTY_NAME "x509_pem_cert"

/* Environment variable that points to the default SSL roots file. This file
   must be a PEM encoded file with all the roots such as the one that can be
   downloaded from https://pki.google.com/roots.pem.  */
#define GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR \
  "GRPC_DEFAULT_SSL_ROOTS_FILE_PATH"

/* Environment variable that points to the google default application
   credentials json key or refresh token. Used in the
   grpc_google_default_credentials_create function. */
#define GRPC_GOOGLE_CREDENTIALS_ENV_VAR "GOOGLE_APPLICATION_CREDENTIALS"

/* Results for the SSL roots override callback. */
typedef enum {
  GRPC_SSL_ROOTS_OVERRIDE_OK,
  GRPC_SSL_ROOTS_OVERRIDE_FAIL_PERMANENTLY, /* Do not try fallback options. */
  GRPC_SSL_ROOTS_OVERRIDE_FAIL
} grpc_ssl_roots_override_result;

typedef enum {
  /* Server does not request client certificate. A client can present a self
     signed or signed certificates if it wishes to do so and they would be
     accepted. */
  GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
  /* Server requests client certificate but does not enforce that the client
     presents a certificate.

     If the client presents a certificate, the client authentication is left to
     the application based on the metadata like certificate etc.

     The key cert pair should still be valid for the SSL connection to be
     established. */
  GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
  /* Server requests client certificate but does not enforce that the client
     presents a certificate.

     If the client presents a certificate, the client authentication is done by
     grpc framework (The client needs to either present a signed cert or skip no
     certificate for a successful connection).

     The key cert pair should still be valid for the SSL connection to be
     established. */
  GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY,
  /* Server requests client certificate but enforces that the client presents a
     certificate.

     If the client presents a certificate, the client authentication is left to
     the application based on the metadata like certificate etc.

     The key cert pair should still be valid for the SSL connection to be
     established. */
  GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
  /* Server requests client certificate but enforces that the client presents a
     certificate.

     The cerificate presented by the client is verified by grpc framework (The
     client needs to present signed certs for a successful connection).

     The key cert pair should still be valid for the SSL connection to be
     established. */
  GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
} grpc_ssl_client_certificate_request_type;

#ifdef __cplusplus
}
#endif

#endif /* GRPC_GRPC_SECURITY_CONSTANTS_H */
