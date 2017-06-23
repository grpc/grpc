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

/** Environment variable that points to the default SSL roots file. This file
   must be a PEM encoded file with all the roots such as the one that can be
   downloaded from https://pki.google.com/roots.pem.  */
#define GRPC_DEFAULT_SSL_ROOTS_FILE_PATH_ENV_VAR \
  "GRPC_DEFAULT_SSL_ROOTS_FILE_PATH"

/** Environment variable that points to the google default application
   credentials json key or refresh token. Used in the
   grpc_google_default_credentials_create function. */
#define GRPC_GOOGLE_CREDENTIALS_ENV_VAR "GOOGLE_APPLICATION_CREDENTIALS"

/** Results for the SSL roots override callback. */
typedef enum {
  GRPC_SSL_ROOTS_OVERRIDE_OK,
  GRPC_SSL_ROOTS_OVERRIDE_FAIL_PERMANENTLY, /** Do not try fallback options. */
  GRPC_SSL_ROOTS_OVERRIDE_FAIL
} grpc_ssl_roots_override_result;

typedef enum {
  /** Server does not request client certificate. A client can present a self
     signed or signed certificates if it wishes to do so and they would be
     accepted. */
  GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE,
  /** Server requests client certificate but does not enforce that the client
     presents a certificate.

     If the client presents a certificate, the client authentication is left to
     the application based on the metadata like certificate etc.

     The key cert pair should still be valid for the SSL connection to be
     established. */
  GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
  /** Server requests client certificate but does not enforce that the client
     presents a certificate.

     If the client presents a certificate, the client authentication is done by
     grpc framework (The client needs to either present a signed cert or skip no
     certificate for a successful connection).

     The key cert pair should still be valid for the SSL connection to be
     established. */
  GRPC_SSL_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY,
  /** Server requests client certificate but enforces that the client presents a
     certificate.

     If the client presents a certificate, the client authentication is left to
     the application based on the metadata like certificate etc.

     The key cert pair should still be valid for the SSL connection to be
     established. */
  GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY,
  /** Server requests client certificate but enforces that the client presents a
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
