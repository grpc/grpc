/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef GRPC_SECURITY_H_
#define GRPC_SECURITY_H_

#include "grpc.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- grpc_credentials object. ---

   A credentials object represents a way to authenticate a client.  */

typedef struct grpc_credentials grpc_credentials;

/* Releases a credentials object.
   The creator of the credentials object is responsible for its release. */
void grpc_credentials_release(grpc_credentials *creds);

/* Creates default credentials. */
grpc_credentials *grpc_default_credentials_create(void);

/* Creates an SSL credentials object.
   - pem_roots_cert is the buffer containing the PEM encoding of the server
     root certificates. This parameter cannot be NULL.
   - pem_roots_cert_size is the size of the associated buffer.
   - pem_private_key is the buffer containing the PEM encoding of the client's
     private key. This parameter can be NULL if the client does not have a
     private key.
   - pem_private_key_size is the size of the associated buffer.
   - pem_cert_chain is the buffer containing the PEM encoding of the client's
     certificate chain. This parameter can be NULL if the client does not have
     a certificate chain.
   - pem_cert_chain_size is the size of the associated buffer. */
grpc_credentials *grpc_ssl_credentials_create(
    const unsigned char *pem_root_certs, size_t pem_root_certs_size,
    const unsigned char *pem_private_key, size_t pem_private_key_size,
    const unsigned char *pem_cert_chain, size_t pem_cert_chain_size);

/* Creates a composite credentials object. */
grpc_credentials *grpc_composite_credentials_create(grpc_credentials *creds1,
                                                    grpc_credentials *creds2);

/* Creates a compute engine credentials object. */
grpc_credentials *grpc_compute_engine_credentials_create(void);

/* Creates a fake transport security credentials object for testing. */
grpc_credentials *grpc_fake_transport_security_credentials_create(void);

/* Creates an IAM credentials object. */
grpc_credentials *grpc_iam_credentials_create(const char *authorization_token,
                                              const char *authority_selector);


/* --- Secure channel creation. --- */

/* The caller of the secure_channel_create functions may override the target
   name used for SSL host name checking using this channel argument which is of
   type GRPC_ARG_STRING. This *should* be used for testing only.
   If this argument is not specified, the name used for SSL host name checking
   will be the target parameter (assuming that the secure channel is an SSL
   channel). If this parameter is specified and the underlying is not an SSL
   channel, it will just be ignored. */
#define GRPC_SSL_TARGET_NAME_OVERRIDE_ARG "grpc.ssl_target_name_override"

/* Creates a default secure channel using the default credentials object using
   the environment. */
grpc_channel *grpc_default_secure_channel_create(const char *target,
                                                 const grpc_channel_args *args);

/* Creates a secure channel using the passed-in credentials. */
grpc_channel *grpc_secure_channel_create(grpc_credentials *creds,
                                         const char *target,
                                         const grpc_channel_args *args);

/* --- grpc_server_credentials object. ---

   A server credentials object represents a way to authenticate a server.  */

typedef struct grpc_server_credentials grpc_server_credentials;

/* Releases a server_credentials object.
   The creator of the server_credentials object is responsible for its release.
   */
void grpc_server_credentials_release(grpc_server_credentials *creds);

/* Creates an SSL server_credentials object.
   TODO(jboeuf): Change the constructor so that it can support multiple
   key/cert pairs.
   - pem_roots_cert is the buffer containing the PEM encoding of the server
     root certificates. This parameter may be NULL if the server does not want
     the client to be authenticated with SSL.
   - pem_roots_cert_size is the size of the associated buffer.
   - pem_private_key is the buffer containing the PEM encoding of the client's
     private key. This parameter cannot be NULL.
   - pem_private_key_size is the size of the associated buffer.
   - pem_cert_chain is the buffer containing the PEM encoding of the client's
     certificate chain. This parameter cannot be NULL.
   - pem_cert_chain_size is the size of the associated buffer. */
grpc_server_credentials *grpc_ssl_server_credentials_create(
    const unsigned char *pem_root_certs, size_t pem_root_certs_size,
    const unsigned char *pem_private_key, size_t pem_private_key_size,
    const unsigned char *pem_cert_chain, size_t pem_cert_chain_size);

/* Creates a fake server transport security credentials object for testing. */
grpc_server_credentials *grpc_fake_transport_security_server_credentials_create(
    void);


/* --- Secure server creation. --- */

/* Creates a secure server using the passed-in server credentials. */
grpc_server *grpc_secure_server_create(grpc_server_credentials *creds,
                                       grpc_completion_queue *cq,
                                       const grpc_channel_args *args);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SECURITY_H_ */
