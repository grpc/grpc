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

#ifndef GRPC_CORE_TSI_SSL_TRANSPORT_SECURITY_H
#define GRPC_CORE_TSI_SSL_TRANSPORT_SECURITY_H

#include "src/core/tsi/transport_security_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Value for the TSI_CERTIFICATE_TYPE_PEER_PROPERTY property for X509 certs. */
#define TSI_X509_CERTIFICATE_TYPE "X509"

/* This property is of type TSI_PEER_PROPERTY_STRING.  */
#define TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY "x509_subject_common_name"
#define TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY \
  "x509_subject_alternative_name"

#define TSI_X509_PEM_CERT_PROPERTY "x509_pem_cert"

#define TSI_SSL_ALPN_SELECTED_PROTOCOL "ssl_alpn_selected_protocol"

/* --- tsi_ssl_client_handshaker_factory object ---

   This object creates a client tsi_handshaker objects implemented in terms of
   the TLS 1.2 specificiation.  */

typedef struct tsi_ssl_client_handshaker_factory
    tsi_ssl_client_handshaker_factory;

/* Object that holds a private key / certificate chain pair in PEM format. */
typedef struct {
  /* private_key is the NULL-terminated string containing the PEM encoding of
     the client's private key. */
  const char *private_key;

  /* cert_chain is the NULL-terminated string containing the PEM encoding of
     the client's certificate chain. */
  const char *cert_chain;
} tsi_ssl_pem_key_cert_pair;

/* Creates a client handshaker factory.
   - pem_key_cert_pair is a pointer to the object containing client's private
     key and certificate chain. This parameter can be NULL if the client does
     not have such a key/cert pair.
   - pem_roots_cert is the NULL-terminated string containing the PEM encoding of
     the client root certificates. This parameter may be NULL if the server does
     not want the client to be authenticated with SSL.
   - cipher_suites contains an optional list of the ciphers that the client
     supports. The format of this string is described in:
     https://www.openssl.org/docs/apps/ciphers.html.
     This parameter can be set to NULL to use the default set of ciphers.
     TODO(jboeuf): Revisit the format of this parameter.
   - alpn_protocols is an array containing the NULL terminated protocol names
     that the handshakers created with this factory support. This parameter can
     be NULL.
   - num_alpn_protocols is the number of alpn protocols and associated lengths
     specified. If this parameter is 0, the other alpn parameters must be NULL.
   - factory is the address of the factory pointer to be created.

   - This method returns TSI_OK on success or TSI_INVALID_PARAMETER in the case
     where a parameter is invalid.  */
tsi_result tsi_create_ssl_client_handshaker_factory(
    const tsi_ssl_pem_key_cert_pair *pem_key_cert_pair,
    const char *pem_root_certs, const char *cipher_suites,
    const char **alpn_protocols, uint16_t num_alpn_protocols,
    tsi_ssl_client_handshaker_factory **factory);

/* Creates a client handshaker.
  - self is the factory from which the handshaker will be created.
  - server_name_indication indicates the name of the server the client is
    trying to connect to which will be relayed to the server using the SNI
    extension.
  - handshaker is the address of the handshaker pointer to be created.

  - This method returns TSI_OK on success or TSI_INVALID_PARAMETER in the case
    where a parameter is invalid.  */
tsi_result tsi_ssl_client_handshaker_factory_create_handshaker(
    tsi_ssl_client_handshaker_factory *self, const char *server_name_indication,
    tsi_handshaker **handshaker);

/* Destroys the handshaker factory. WARNING: it is unsafe to destroy a factory
   while handshakers created with this factory are still in use.  */
void tsi_ssl_client_handshaker_factory_destroy(
    tsi_ssl_client_handshaker_factory *self);

/* --- tsi_ssl_server_handshaker_factory object ---

   This object creates a client tsi_handshaker objects implemented in terms of
   the TLS 1.2 specificiation.  */

typedef struct tsi_ssl_server_handshaker_factory
    tsi_ssl_server_handshaker_factory;

/* Creates a server handshaker factory.
   - pem_key_cert_pairs is an array private key / certificate chains of the
     server.
   - num_key_cert_pairs is the number of items in the pem_key_cert_pairs array.
   - pem_root_certs is the NULL-terminated string containing the PEM encoding
     of the server root certificates.
   - cipher_suites contains an optional list of the ciphers that the server
     supports. The format of this string is described in:
     https://www.openssl.org/docs/apps/ciphers.html.
     This parameter can be set to NULL to use the default set of ciphers.
     TODO(jboeuf): Revisit the format of this parameter.
   - alpn_protocols is an array containing the NULL terminated protocol names
     that the handshakers created with this factory support. This parameter can
     be NULL.
   - num_alpn_protocols is the number of alpn protocols and associated lengths
     specified. If this parameter is 0, the other alpn parameters must be NULL.
   - factory is the address of the factory pointer to be created.

   - This method returns TSI_OK on success or TSI_INVALID_PARAMETER in the case
     where a parameter is invalid.  */
tsi_result tsi_create_ssl_server_handshaker_factory(
    const tsi_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs, const char *pem_client_root_certs,
    int force_client_auth, const char *cipher_suites,
    const char **alpn_protocols, uint16_t num_alpn_protocols,
    tsi_ssl_server_handshaker_factory **factory);

/* Same as tsi_create_ssl_server_handshaker_factory method except uses
   tsi_client_certificate_request_type to support more ways to handle client
   certificate authentication.
   - client_certificate_request, if set to non-zero will force the client to
     authenticate with an SSL cert. Note that this option is ignored if
     pem_client_root_certs is NULL or pem_client_roots_certs_size is 0 */
tsi_result tsi_create_ssl_server_handshaker_factory_ex(
    const tsi_ssl_pem_key_cert_pair *pem_key_cert_pairs,
    size_t num_key_cert_pairs, const char *pem_client_root_certs,
    tsi_client_certificate_request_type client_certificate_request,
    const char *cipher_suites, const char **alpn_protocols,
    uint16_t num_alpn_protocols, tsi_ssl_server_handshaker_factory **factory);

/* Creates a server handshaker.
  - self is the factory from which the handshaker will be created.
  - handshaker is the address of the handshaker pointer to be created.

  - This method returns TSI_OK on success or TSI_INVALID_PARAMETER in the case
    where a parameter is invalid.  */
tsi_result tsi_ssl_server_handshaker_factory_create_handshaker(
    tsi_ssl_server_handshaker_factory *self, tsi_handshaker **handshaker);

/* Destroys the handshaker factory. WARNING: it is unsafe to destroy a factory
   while handshakers created with this factory are still in use.  */
void tsi_ssl_server_handshaker_factory_destroy(
    tsi_ssl_server_handshaker_factory *self);

/* Util that checks that an ssl peer matches a specific name.
   Still TODO(jboeuf):
   - handle mixed case.
   - handle %encoded chars.
   - handle public suffix wildchar more strictly (e.g. *.co.uk) */
int tsi_ssl_peer_matches_name(const tsi_peer *peer, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_TSI_SSL_TRANSPORT_SECURITY_H */
