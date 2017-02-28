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

#include "src/core/lib/tsi/ssl_transport_security.h"

#include <grpc/support/port_platform.h>

#include <limits.h>
#include <string.h>

/* TODO(jboeuf): refactor inet_ntop into a portability header. */
/* Note: for whomever reads this and tries to refactor this, this
   can't be in grpc, it has to be in gpr. */
#ifdef GPR_WINDOWS
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>

#include <openssl/bio.h>
#include <openssl/crypto.h> /* For OPENSSL_free */
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "src/core/lib/tsi/ssl_types.h"
#include "src/core/lib/tsi/transport_security.h"

/* --- Constants. ---*/

#define TSI_SSL_MAX_PROTECTED_FRAME_SIZE_UPPER_BOUND 16384
#define TSI_SSL_MAX_PROTECTED_FRAME_SIZE_LOWER_BOUND 1024

/* Putting a macro like this and littering the source file with #if is really
   bad practice.
   TODO(jboeuf): refactor all the #if / #endif in a separate module. */
#ifndef TSI_OPENSSL_ALPN_SUPPORT
#define TSI_OPENSSL_ALPN_SUPPORT 1
#endif

/* TODO(jboeuf): I have not found a way to get this number dynamically from the
   SSL structure. This is what we would ultimately want though... */
#define TSI_SSL_MAX_PROTECTION_OVERHEAD 100

/* --- Structure definitions. ---*/

struct tsi_ssl_handshaker_factory {
  tsi_result (*create_handshaker)(tsi_ssl_handshaker_factory *self,
                                  const char *server_name_indication,
                                  tsi_handshaker **handshaker);
  void (*destroy)(tsi_ssl_handshaker_factory *self);
};

typedef struct {
  tsi_ssl_handshaker_factory base;
  SSL_CTX *ssl_context;
  unsigned char *alpn_protocol_list;
  size_t alpn_protocol_list_length;
} tsi_ssl_client_handshaker_factory;

typedef struct {
  tsi_ssl_handshaker_factory base;

  /* Several contexts to support SNI.
     The tsi_peer array contains the subject names of the server certificates
     associated with the contexts at the same index.  */
  SSL_CTX **ssl_contexts;
  tsi_peer *ssl_context_x509_subject_names;
  size_t ssl_context_count;
  unsigned char *alpn_protocol_list;
  size_t alpn_protocol_list_length;
} tsi_ssl_server_handshaker_factory;

typedef struct {
  tsi_handshaker base;
  SSL *ssl;
  BIO *into_ssl;
  BIO *from_ssl;
  tsi_result result;
} tsi_ssl_handshaker;

typedef struct {
  tsi_frame_protector base;
  SSL *ssl;
  BIO *into_ssl;
  BIO *from_ssl;
  unsigned char *buffer;
  size_t buffer_size;
  size_t buffer_offset;
} tsi_ssl_frame_protector;

/* --- Library Initialization. ---*/

static gpr_once init_openssl_once = GPR_ONCE_INIT;
static gpr_mu *openssl_mutexes = NULL;

static void openssl_locking_cb(int mode, int type, const char *file, int line) {
  if (mode & CRYPTO_LOCK) {
    gpr_mu_lock(&openssl_mutexes[type]);
  } else {
    gpr_mu_unlock(&openssl_mutexes[type]);
  }
}

static unsigned long openssl_thread_id_cb(void) {
  return (unsigned long)gpr_thd_currentid();
}

static void init_openssl(void) {
  int i;
  int num_locks;
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
  num_locks = CRYPTO_num_locks();
  GPR_ASSERT(num_locks > 0);
  openssl_mutexes = gpr_malloc((size_t)num_locks * sizeof(gpr_mu));
  for (i = 0; i < CRYPTO_num_locks(); i++) {
    gpr_mu_init(&openssl_mutexes[i]);
  }
  CRYPTO_set_locking_callback(openssl_locking_cb);
  CRYPTO_set_id_callback(openssl_thread_id_cb);
}

/* --- Ssl utils. ---*/

static const char *ssl_error_string(int error) {
  switch (error) {
    case SSL_ERROR_NONE:
      return "SSL_ERROR_NONE";
    case SSL_ERROR_ZERO_RETURN:
      return "SSL_ERROR_ZERO_RETURN";
    case SSL_ERROR_WANT_READ:
      return "SSL_ERROR_WANT_READ";
    case SSL_ERROR_WANT_WRITE:
      return "SSL_ERROR_WANT_WRITE";
    case SSL_ERROR_WANT_CONNECT:
      return "SSL_ERROR_WANT_CONNECT";
    case SSL_ERROR_WANT_ACCEPT:
      return "SSL_ERROR_WANT_ACCEPT";
    case SSL_ERROR_WANT_X509_LOOKUP:
      return "SSL_ERROR_WANT_X509_LOOKUP";
    case SSL_ERROR_SYSCALL:
      return "SSL_ERROR_SYSCALL";
    case SSL_ERROR_SSL:
      return "SSL_ERROR_SSL";
    default:
      return "Unknown error";
  }
}

/* TODO(jboeuf): Remove when we are past the debugging phase with this code. */
static void ssl_log_where_info(const SSL *ssl, int where, int flag,
                               const char *msg) {
  if ((where & flag) && tsi_tracing_enabled) {
    gpr_log(GPR_INFO, "%20.20s - %30.30s  - %5.10s", msg,
            SSL_state_string_long(ssl), SSL_state_string(ssl));
  }
}

/* Used for debugging. TODO(jboeuf): Remove when code is mature enough. */
static void ssl_info_callback(const SSL *ssl, int where, int ret) {
  if (ret == 0) {
    gpr_log(GPR_ERROR, "ssl_info_callback: error occured.\n");
    return;
  }

  ssl_log_where_info(ssl, where, SSL_CB_LOOP, "LOOP");
  ssl_log_where_info(ssl, where, SSL_CB_HANDSHAKE_START, "HANDSHAKE START");
  ssl_log_where_info(ssl, where, SSL_CB_HANDSHAKE_DONE, "HANDSHAKE DONE");
}

/* Returns 1 if name looks like an IP address, 0 otherwise.
   This is a very rough heuristic, and only handles IPv6 in hexadecimal form. */
static int looks_like_ip_address(const char *name) {
  size_t i;
  size_t dot_count = 0;
  size_t num_size = 0;
  for (i = 0; i < strlen(name); i++) {
    if (name[i] == ':') {
      /* IPv6 Address in hexadecimal form, : is not allowed in DNS names. */
      return 1;
    }
    if (name[i] >= '0' && name[i] <= '9') {
      if (num_size > 3) return 0;
      num_size++;
    } else if (name[i] == '.') {
      if (dot_count > 3 || num_size == 0) return 0;
      dot_count++;
      num_size = 0;
    } else {
      return 0;
    }
  }
  if (dot_count < 3 || num_size == 0) return 0;
  return 1;
}

/* Gets the subject CN from an X509 cert. */
static tsi_result ssl_get_x509_common_name(X509 *cert, unsigned char **utf8,
                                           size_t *utf8_size) {
  int common_name_index = -1;
  X509_NAME_ENTRY *common_name_entry = NULL;
  ASN1_STRING *common_name_asn1 = NULL;
  X509_NAME *subject_name = X509_get_subject_name(cert);
  int utf8_returned_size = 0;
  if (subject_name == NULL) {
    gpr_log(GPR_ERROR, "Could not get subject name from certificate.");
    return TSI_NOT_FOUND;
  }
  common_name_index =
      X509_NAME_get_index_by_NID(subject_name, NID_commonName, -1);
  if (common_name_index == -1) {
    gpr_log(GPR_ERROR,
            "Could not get common name of subject from certificate.");
    return TSI_NOT_FOUND;
  }
  common_name_entry = X509_NAME_get_entry(subject_name, common_name_index);
  if (common_name_entry == NULL) {
    gpr_log(GPR_ERROR, "Could not get common name entry from certificate.");
    return TSI_INTERNAL_ERROR;
  }
  common_name_asn1 = X509_NAME_ENTRY_get_data(common_name_entry);
  if (common_name_asn1 == NULL) {
    gpr_log(GPR_ERROR,
            "Could not get common name entry asn1 from certificate.");
    return TSI_INTERNAL_ERROR;
  }
  utf8_returned_size = ASN1_STRING_to_UTF8(utf8, common_name_asn1);
  if (utf8_returned_size < 0) {
    gpr_log(GPR_ERROR, "Could not extract utf8 from asn1 string.");
    return TSI_OUT_OF_RESOURCES;
  }
  *utf8_size = (size_t)utf8_returned_size;
  return TSI_OK;
}

/* Gets the subject CN of an X509 cert as a tsi_peer_property. */
static tsi_result peer_property_from_x509_common_name(
    X509 *cert, tsi_peer_property *property) {
  unsigned char *common_name;
  size_t common_name_size;
  tsi_result result =
      ssl_get_x509_common_name(cert, &common_name, &common_name_size);
  if (result != TSI_OK) {
    if (result == TSI_NOT_FOUND) {
      common_name = NULL;
      common_name_size = 0;
    } else {
      return result;
    }
  }
  result = tsi_construct_string_peer_property(
      TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY,
      common_name == NULL ? "" : (const char *)common_name, common_name_size,
      property);
  OPENSSL_free(common_name);
  return result;
}

/* Gets the X509 cert in PEM format as a tsi_peer_property. */
static tsi_result add_pem_certificate(X509 *cert, tsi_peer_property *property) {
  BIO *bio = BIO_new(BIO_s_mem());
  if (!PEM_write_bio_X509(bio, cert)) {
    BIO_free(bio);
    return TSI_INTERNAL_ERROR;
  }
  char *contents;
  long len = BIO_get_mem_data(bio, &contents);
  if (len <= 0) {
    BIO_free(bio);
    return TSI_INTERNAL_ERROR;
  }
  tsi_result result = tsi_construct_string_peer_property(
      TSI_X509_PEM_CERT_PROPERTY, (const char *)contents, (size_t)len,
      property);
  BIO_free(bio);
  return result;
}

/* Gets the subject SANs from an X509 cert as a tsi_peer_property. */
static tsi_result add_subject_alt_names_properties_to_peer(
    tsi_peer *peer, GENERAL_NAMES *subject_alt_names,
    size_t subject_alt_name_count) {
  size_t i;
  tsi_result result = TSI_OK;

  /* Reset for DNS entries filtering. */
  peer->property_count -= subject_alt_name_count;

  for (i = 0; i < subject_alt_name_count; i++) {
    GENERAL_NAME *subject_alt_name =
        sk_GENERAL_NAME_value(subject_alt_names, TSI_SIZE_AS_SIZE(i));
    /* Filter out the non-dns entries names. */
    if (subject_alt_name->type == GEN_DNS) {
      unsigned char *name = NULL;
      int name_size;
      name_size = ASN1_STRING_to_UTF8(&name, subject_alt_name->d.dNSName);
      if (name_size < 0) {
        gpr_log(GPR_ERROR, "Could not get utf8 from asn1 string.");
        result = TSI_INTERNAL_ERROR;
        break;
      }
      result = tsi_construct_string_peer_property(
          TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, (const char *)name,
          (size_t)name_size, &peer->properties[peer->property_count++]);
      OPENSSL_free(name);
    } else if (subject_alt_name->type == GEN_IPADD) {
      char ntop_buf[INET6_ADDRSTRLEN];
      int af;

      if (subject_alt_name->d.iPAddress->length == 4) {
        af = AF_INET;
      } else if (subject_alt_name->d.iPAddress->length == 16) {
        af = AF_INET6;
      } else {
        gpr_log(GPR_ERROR, "SAN IP Address contained invalid IP");
        result = TSI_INTERNAL_ERROR;
        break;
      }
      const char *name = inet_ntop(af, subject_alt_name->d.iPAddress->data,
                                   ntop_buf, INET6_ADDRSTRLEN);
      if (name == NULL) {
        gpr_log(GPR_ERROR, "Could not get IP string from asn1 octet.");
        result = TSI_INTERNAL_ERROR;
        break;
      }

      result = tsi_construct_string_peer_property_from_cstring(
          TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, name,
          &peer->properties[peer->property_count++]);
    }
    if (result != TSI_OK) break;
  }
  return result;
}

/* Gets information about the peer's X509 cert as a tsi_peer object. */
static tsi_result peer_from_x509(X509 *cert, int include_certificate_type,
                                 tsi_peer *peer) {
  /* TODO(jboeuf): Maybe add more properties. */
  GENERAL_NAMES *subject_alt_names =
      X509_get_ext_d2i(cert, NID_subject_alt_name, 0, 0);
  int subject_alt_name_count = (subject_alt_names != NULL)
                                   ? (int)sk_GENERAL_NAME_num(subject_alt_names)
                                   : 0;
  size_t property_count;
  tsi_result result;
  GPR_ASSERT(subject_alt_name_count >= 0);
  property_count = (include_certificate_type ? (size_t)1 : 0) +
                   2 /* common name, certificate */ +
                   (size_t)subject_alt_name_count;
  result = tsi_construct_peer(property_count, peer);
  if (result != TSI_OK) return result;
  do {
    if (include_certificate_type) {
      result = tsi_construct_string_peer_property_from_cstring(
          TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
          &peer->properties[0]);
      if (result != TSI_OK) break;
    }
    result = peer_property_from_x509_common_name(
        cert, &peer->properties[include_certificate_type ? 1 : 0]);
    if (result != TSI_OK) break;

    result = add_pem_certificate(
        cert, &peer->properties[include_certificate_type ? 2 : 1]);
    if (result != TSI_OK) break;

    if (subject_alt_name_count != 0) {
      result = add_subject_alt_names_properties_to_peer(
          peer, subject_alt_names, (size_t)subject_alt_name_count);
      if (result != TSI_OK) break;
    }
  } while (0);

  if (subject_alt_names != NULL) {
    sk_GENERAL_NAME_pop_free(subject_alt_names, GENERAL_NAME_free);
  }
  if (result != TSI_OK) tsi_peer_destruct(peer);
  return result;
}

/* Logs the SSL error stack. */
static void log_ssl_error_stack(void) {
  unsigned long err;
  while ((err = ERR_get_error()) != 0) {
    char details[256];
    ERR_error_string_n((uint32_t)err, details, sizeof(details));
    gpr_log(GPR_ERROR, "%s", details);
  }
}

/* Performs an SSL_read and handle errors. */
static tsi_result do_ssl_read(SSL *ssl, unsigned char *unprotected_bytes,
                              size_t *unprotected_bytes_size) {
  int read_from_ssl;
  GPR_ASSERT(*unprotected_bytes_size <= INT_MAX);
  read_from_ssl =
      SSL_read(ssl, unprotected_bytes, (int)*unprotected_bytes_size);
  if (read_from_ssl == 0) {
    gpr_log(GPR_ERROR, "SSL_read returned 0 unexpectedly.");
    return TSI_INTERNAL_ERROR;
  }
  if (read_from_ssl < 0) {
    read_from_ssl = SSL_get_error(ssl, read_from_ssl);
    switch (read_from_ssl) {
      case SSL_ERROR_WANT_READ:
        /* We need more data to finish the frame. */
        *unprotected_bytes_size = 0;
        return TSI_OK;
      case SSL_ERROR_WANT_WRITE:
        gpr_log(
            GPR_ERROR,
            "Peer tried to renegotiate SSL connection. This is unsupported.");
        return TSI_UNIMPLEMENTED;
      case SSL_ERROR_SSL:
        gpr_log(GPR_ERROR, "Corruption detected.");
        log_ssl_error_stack();
        return TSI_DATA_CORRUPTED;
      default:
        gpr_log(GPR_ERROR, "SSL_read failed with error %s.",
                ssl_error_string(read_from_ssl));
        return TSI_PROTOCOL_FAILURE;
    }
  }
  *unprotected_bytes_size = (size_t)read_from_ssl;
  return TSI_OK;
}

/* Performs an SSL_write and handle errors. */
static tsi_result do_ssl_write(SSL *ssl, unsigned char *unprotected_bytes,
                               size_t unprotected_bytes_size) {
  int ssl_write_result;
  GPR_ASSERT(unprotected_bytes_size <= INT_MAX);
  ssl_write_result =
      SSL_write(ssl, unprotected_bytes, (int)unprotected_bytes_size);
  if (ssl_write_result < 0) {
    ssl_write_result = SSL_get_error(ssl, ssl_write_result);
    if (ssl_write_result == SSL_ERROR_WANT_READ) {
      gpr_log(GPR_ERROR,
              "Peer tried to renegotiate SSL connection. This is unsupported.");
      return TSI_UNIMPLEMENTED;
    } else {
      gpr_log(GPR_ERROR, "SSL_write failed with error %s.",
              ssl_error_string(ssl_write_result));
      return TSI_INTERNAL_ERROR;
    }
  }
  return TSI_OK;
}

/* Loads an in-memory PEM certificate chain into the SSL context. */
static tsi_result ssl_ctx_use_certificate_chain(
    SSL_CTX *context, const unsigned char *pem_cert_chain,
    size_t pem_cert_chain_size) {
  tsi_result result = TSI_OK;
  X509 *certificate = NULL;
  BIO *pem;
  GPR_ASSERT(pem_cert_chain_size <= INT_MAX);
  pem = BIO_new_mem_buf((void *)pem_cert_chain, (int)pem_cert_chain_size);
  if (pem == NULL) return TSI_OUT_OF_RESOURCES;

  do {
    certificate = PEM_read_bio_X509_AUX(pem, NULL, NULL, "");
    if (certificate == NULL) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    if (!SSL_CTX_use_certificate(context, certificate)) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    while (1) {
      X509 *certificate_authority = PEM_read_bio_X509(pem, NULL, NULL, "");
      if (certificate_authority == NULL) {
        ERR_clear_error();
        break; /* Done reading. */
      }
      if (!SSL_CTX_add_extra_chain_cert(context, certificate_authority)) {
        X509_free(certificate_authority);
        result = TSI_INVALID_ARGUMENT;
        break;
      }
      /* We don't need to free certificate_authority as its ownership has been
         transfered to the context. That is not the case for certificate though.
       */
    }
  } while (0);

  if (certificate != NULL) X509_free(certificate);
  BIO_free(pem);
  return result;
}

/* Loads an in-memory PEM private key into the SSL context. */
static tsi_result ssl_ctx_use_private_key(SSL_CTX *context,
                                          const unsigned char *pem_key,
                                          size_t pem_key_size) {
  tsi_result result = TSI_OK;
  EVP_PKEY *private_key = NULL;
  BIO *pem;
  GPR_ASSERT(pem_key_size <= INT_MAX);
  pem = BIO_new_mem_buf((void *)pem_key, (int)pem_key_size);
  if (pem == NULL) return TSI_OUT_OF_RESOURCES;
  do {
    private_key = PEM_read_bio_PrivateKey(pem, NULL, NULL, "");
    if (private_key == NULL) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    if (!SSL_CTX_use_PrivateKey(context, private_key)) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
  } while (0);
  if (private_key != NULL) EVP_PKEY_free(private_key);
  BIO_free(pem);
  return result;
}

/* Loads in-memory PEM verification certs into the SSL context and optionally
   returns the verification cert names (root_names can be NULL). */
static tsi_result ssl_ctx_load_verification_certs(
    SSL_CTX *context, const unsigned char *pem_roots, size_t pem_roots_size,
    STACK_OF(X509_NAME) * *root_names) {
  tsi_result result = TSI_OK;
  size_t num_roots = 0;
  X509 *root = NULL;
  X509_NAME *root_name = NULL;
  BIO *pem;
  X509_STORE *root_store;
  GPR_ASSERT(pem_roots_size <= INT_MAX);
  pem = BIO_new_mem_buf((void *)pem_roots, (int)pem_roots_size);
  root_store = SSL_CTX_get_cert_store(context);
  if (root_store == NULL) return TSI_INVALID_ARGUMENT;
  if (pem == NULL) return TSI_OUT_OF_RESOURCES;
  if (root_names != NULL) {
    *root_names = sk_X509_NAME_new_null();
    if (*root_names == NULL) return TSI_OUT_OF_RESOURCES;
  }

  while (1) {
    root = PEM_read_bio_X509_AUX(pem, NULL, NULL, "");
    if (root == NULL) {
      ERR_clear_error();
      break; /* We're at the end of stream. */
    }
    if (root_names != NULL) {
      root_name = X509_get_subject_name(root);
      if (root_name == NULL) {
        gpr_log(GPR_ERROR, "Could not get name from root certificate.");
        result = TSI_INVALID_ARGUMENT;
        break;
      }
      root_name = X509_NAME_dup(root_name);
      if (root_name == NULL) {
        result = TSI_OUT_OF_RESOURCES;
        break;
      }
      sk_X509_NAME_push(*root_names, root_name);
      root_name = NULL;
    }
    if (!X509_STORE_add_cert(root_store, root)) {
      gpr_log(GPR_ERROR, "Could not add root certificate to ssl context.");
      result = TSI_INTERNAL_ERROR;
      break;
    }
    X509_free(root);
    num_roots++;
  }

  if (num_roots == 0) {
    gpr_log(GPR_ERROR, "Could not load any root certificate.");
    result = TSI_INVALID_ARGUMENT;
  }

  if (result != TSI_OK) {
    if (root != NULL) X509_free(root);
    if (root_names != NULL) {
      sk_X509_NAME_pop_free(*root_names, X509_NAME_free);
      *root_names = NULL;
      if (root_name != NULL) X509_NAME_free(root_name);
    }
  }
  BIO_free(pem);
  return result;
}

/* Populates the SSL context with a private key and a cert chain, and sets the
   cipher list and the ephemeral ECDH key. */
static tsi_result populate_ssl_context(
    SSL_CTX *context, const unsigned char *pem_private_key,
    size_t pem_private_key_size, const unsigned char *pem_certificate_chain,
    size_t pem_certificate_chain_size, const char *cipher_list) {
  tsi_result result = TSI_OK;
  if (pem_certificate_chain != NULL) {
    result = ssl_ctx_use_certificate_chain(context, pem_certificate_chain,
                                           pem_certificate_chain_size);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Invalid cert chain file.");
      return result;
    }
  }
  if (pem_private_key != NULL) {
    result =
        ssl_ctx_use_private_key(context, pem_private_key, pem_private_key_size);
    if (result != TSI_OK || !SSL_CTX_check_private_key(context)) {
      gpr_log(GPR_ERROR, "Invalid private key.");
      return result != TSI_OK ? result : TSI_INVALID_ARGUMENT;
    }
  }
  if ((cipher_list != NULL) && !SSL_CTX_set_cipher_list(context, cipher_list)) {
    gpr_log(GPR_ERROR, "Invalid cipher list: %s.", cipher_list);
    return TSI_INVALID_ARGUMENT;
  }
  {
    EC_KEY *ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!SSL_CTX_set_tmp_ecdh(context, ecdh)) {
      gpr_log(GPR_ERROR, "Could not set ephemeral ECDH key.");
      EC_KEY_free(ecdh);
      return TSI_INTERNAL_ERROR;
    }
    SSL_CTX_set_options(context, SSL_OP_SINGLE_ECDH_USE);
    EC_KEY_free(ecdh);
  }
  return TSI_OK;
}

/* Extracts the CN and the SANs from an X509 cert as a peer object. */
static tsi_result extract_x509_subject_names_from_pem_cert(
    const unsigned char *pem_cert, size_t pem_cert_size, tsi_peer *peer) {
  tsi_result result = TSI_OK;
  X509 *cert = NULL;
  BIO *pem;
  GPR_ASSERT(pem_cert_size <= INT_MAX);
  pem = BIO_new_mem_buf((void *)pem_cert, (int)pem_cert_size);
  if (pem == NULL) return TSI_OUT_OF_RESOURCES;

  cert = PEM_read_bio_X509(pem, NULL, NULL, "");
  if (cert == NULL) {
    gpr_log(GPR_ERROR, "Invalid certificate");
    result = TSI_INVALID_ARGUMENT;
  } else {
    result = peer_from_x509(cert, 0, peer);
  }
  if (cert != NULL) X509_free(cert);
  BIO_free(pem);
  return result;
}

/* Builds the alpn protocol name list according to rfc 7301. */
static tsi_result build_alpn_protocol_name_list(
    const unsigned char **alpn_protocols,
    const unsigned char *alpn_protocols_lengths, uint16_t num_alpn_protocols,
    unsigned char **protocol_name_list, size_t *protocol_name_list_length) {
  uint16_t i;
  unsigned char *current;
  *protocol_name_list = NULL;
  *protocol_name_list_length = 0;
  if (num_alpn_protocols == 0) return TSI_INVALID_ARGUMENT;
  for (i = 0; i < num_alpn_protocols; i++) {
    if (alpn_protocols_lengths[i] == 0) {
      gpr_log(GPR_ERROR, "Invalid 0-length protocol name.");
      return TSI_INVALID_ARGUMENT;
    }
    *protocol_name_list_length += (size_t)alpn_protocols_lengths[i] + 1;
  }
  *protocol_name_list = gpr_malloc(*protocol_name_list_length);
  if (*protocol_name_list == NULL) return TSI_OUT_OF_RESOURCES;
  current = *protocol_name_list;
  for (i = 0; i < num_alpn_protocols; i++) {
    *(current++) = alpn_protocols_lengths[i];
    memcpy(current, alpn_protocols[i], alpn_protocols_lengths[i]);
    current += alpn_protocols_lengths[i];
  }
  /* Safety check. */
  if ((current < *protocol_name_list) ||
      ((uintptr_t)(current - *protocol_name_list) !=
       *protocol_name_list_length)) {
    return TSI_INTERNAL_ERROR;
  }
  return TSI_OK;
}

// The verification callback is used for clients that don't really care about
// the server's certificate, but we need to pull it anyway, in case a higher
// layer wants to look at it. In this case the verification may fail, but
// we don't really care.
static int NullVerifyCallback(int preverify_ok, X509_STORE_CTX *ctx) {
  return 1;
}

/* --- tsi_frame_protector methods implementation. ---*/

static tsi_result ssl_protector_protect(tsi_frame_protector *self,
                                        const unsigned char *unprotected_bytes,
                                        size_t *unprotected_bytes_size,
                                        unsigned char *protected_output_frames,
                                        size_t *protected_output_frames_size) {
  tsi_ssl_frame_protector *impl = (tsi_ssl_frame_protector *)self;
  int read_from_ssl;
  size_t available;
  tsi_result result = TSI_OK;

  /* First see if we have some pending data in the SSL BIO. */
  int pending_in_ssl = (int)BIO_pending(impl->from_ssl);
  if (pending_in_ssl > 0) {
    *unprotected_bytes_size = 0;
    GPR_ASSERT(*protected_output_frames_size <= INT_MAX);
    read_from_ssl = BIO_read(impl->from_ssl, protected_output_frames,
                             (int)*protected_output_frames_size);
    if (read_from_ssl < 0) {
      gpr_log(GPR_ERROR,
              "Could not read from BIO even though some data is pending");
      return TSI_INTERNAL_ERROR;
    }
    *protected_output_frames_size = (size_t)read_from_ssl;
    return TSI_OK;
  }

  /* Now see if we can send a complete frame. */
  available = impl->buffer_size - impl->buffer_offset;
  if (available > *unprotected_bytes_size) {
    /* If we cannot, just copy the data in our internal buffer. */
    memcpy(impl->buffer + impl->buffer_offset, unprotected_bytes,
           *unprotected_bytes_size);
    impl->buffer_offset += *unprotected_bytes_size;
    *protected_output_frames_size = 0;
    return TSI_OK;
  }

  /* If we can, prepare the buffer, send it to SSL_write and read. */
  memcpy(impl->buffer + impl->buffer_offset, unprotected_bytes, available);
  result = do_ssl_write(impl->ssl, impl->buffer, impl->buffer_size);
  if (result != TSI_OK) return result;

  GPR_ASSERT(*protected_output_frames_size <= INT_MAX);
  read_from_ssl = BIO_read(impl->from_ssl, protected_output_frames,
                           (int)*protected_output_frames_size);
  if (read_from_ssl < 0) {
    gpr_log(GPR_ERROR, "Could not read from BIO after SSL_write.");
    return TSI_INTERNAL_ERROR;
  }
  *protected_output_frames_size = (size_t)read_from_ssl;
  *unprotected_bytes_size = available;
  impl->buffer_offset = 0;
  return TSI_OK;
}

static tsi_result ssl_protector_protect_flush(
    tsi_frame_protector *self, unsigned char *protected_output_frames,
    size_t *protected_output_frames_size, size_t *still_pending_size) {
  tsi_result result = TSI_OK;
  tsi_ssl_frame_protector *impl = (tsi_ssl_frame_protector *)self;
  int read_from_ssl = 0;
  int pending;

  if (impl->buffer_offset != 0) {
    result = do_ssl_write(impl->ssl, impl->buffer, impl->buffer_offset);
    if (result != TSI_OK) return result;
    impl->buffer_offset = 0;
  }

  pending = (int)BIO_pending(impl->from_ssl);
  GPR_ASSERT(pending >= 0);
  *still_pending_size = (size_t)pending;
  if (*still_pending_size == 0) return TSI_OK;

  GPR_ASSERT(*protected_output_frames_size <= INT_MAX);
  read_from_ssl = BIO_read(impl->from_ssl, protected_output_frames,
                           (int)*protected_output_frames_size);
  if (read_from_ssl <= 0) {
    gpr_log(GPR_ERROR, "Could not read from BIO after SSL_write.");
    return TSI_INTERNAL_ERROR;
  }
  *protected_output_frames_size = (size_t)read_from_ssl;
  pending = (int)BIO_pending(impl->from_ssl);
  GPR_ASSERT(pending >= 0);
  *still_pending_size = (size_t)pending;
  return TSI_OK;
}

static tsi_result ssl_protector_unprotect(
    tsi_frame_protector *self, const unsigned char *protected_frames_bytes,
    size_t *protected_frames_bytes_size, unsigned char *unprotected_bytes,
    size_t *unprotected_bytes_size) {
  tsi_result result = TSI_OK;
  int written_into_ssl = 0;
  size_t output_bytes_size = *unprotected_bytes_size;
  size_t output_bytes_offset = 0;
  tsi_ssl_frame_protector *impl = (tsi_ssl_frame_protector *)self;

  /* First, try to read remaining data from ssl. */
  result = do_ssl_read(impl->ssl, unprotected_bytes, unprotected_bytes_size);
  if (result != TSI_OK) return result;
  if (*unprotected_bytes_size == output_bytes_size) {
    /* We have read everything we could and cannot process any more input. */
    *protected_frames_bytes_size = 0;
    return TSI_OK;
  }
  output_bytes_offset = *unprotected_bytes_size;
  unprotected_bytes += output_bytes_offset;
  *unprotected_bytes_size = output_bytes_size - output_bytes_offset;

  /* Then, try to write some data to ssl. */
  GPR_ASSERT(*protected_frames_bytes_size <= INT_MAX);
  written_into_ssl = BIO_write(impl->into_ssl, protected_frames_bytes,
                               (int)*protected_frames_bytes_size);
  if (written_into_ssl < 0) {
    gpr_log(GPR_ERROR, "Sending protected frame to ssl failed with %d",
            written_into_ssl);
    return TSI_INTERNAL_ERROR;
  }
  *protected_frames_bytes_size = (size_t)written_into_ssl;

  /* Now try to read some data again. */
  result = do_ssl_read(impl->ssl, unprotected_bytes, unprotected_bytes_size);
  if (result == TSI_OK) {
    /* Don't forget to output the total number of bytes read. */
    *unprotected_bytes_size += output_bytes_offset;
  }
  return result;
}

static void ssl_protector_destroy(tsi_frame_protector *self) {
  tsi_ssl_frame_protector *impl = (tsi_ssl_frame_protector *)self;
  if (impl->buffer != NULL) gpr_free(impl->buffer);
  if (impl->ssl != NULL) SSL_free(impl->ssl);
  gpr_free(self);
}

static const tsi_frame_protector_vtable frame_protector_vtable = {
    ssl_protector_protect, ssl_protector_protect_flush, ssl_protector_unprotect,
    ssl_protector_destroy,
};

/* --- tsi_handshaker methods implementation. ---*/

static tsi_result ssl_handshaker_get_bytes_to_send_to_peer(tsi_handshaker *self,
                                                           unsigned char *bytes,
                                                           size_t *bytes_size) {
  tsi_ssl_handshaker *impl = (tsi_ssl_handshaker *)self;
  int bytes_read_from_ssl = 0;
  if (bytes == NULL || bytes_size == NULL || *bytes_size == 0 ||
      *bytes_size > INT_MAX) {
    return TSI_INVALID_ARGUMENT;
  }
  GPR_ASSERT(*bytes_size <= INT_MAX);
  bytes_read_from_ssl = BIO_read(impl->from_ssl, bytes, (int)*bytes_size);
  if (bytes_read_from_ssl < 0) {
    *bytes_size = 0;
    if (!BIO_should_retry(impl->from_ssl)) {
      impl->result = TSI_INTERNAL_ERROR;
      return impl->result;
    } else {
      return TSI_OK;
    }
  }
  *bytes_size = (size_t)bytes_read_from_ssl;
  return BIO_pending(impl->from_ssl) == 0 ? TSI_OK : TSI_INCOMPLETE_DATA;
}

static tsi_result ssl_handshaker_get_result(tsi_handshaker *self) {
  tsi_ssl_handshaker *impl = (tsi_ssl_handshaker *)self;
  if ((impl->result == TSI_HANDSHAKE_IN_PROGRESS) &&
      SSL_is_init_finished(impl->ssl)) {
    impl->result = TSI_OK;
  }
  return impl->result;
}

static tsi_result ssl_handshaker_process_bytes_from_peer(
    tsi_handshaker *self, const unsigned char *bytes, size_t *bytes_size) {
  tsi_ssl_handshaker *impl = (tsi_ssl_handshaker *)self;
  int bytes_written_into_ssl_size = 0;
  if (bytes == NULL || bytes_size == 0 || *bytes_size > INT_MAX) {
    return TSI_INVALID_ARGUMENT;
  }
  GPR_ASSERT(*bytes_size <= INT_MAX);
  bytes_written_into_ssl_size =
      BIO_write(impl->into_ssl, bytes, (int)*bytes_size);
  if (bytes_written_into_ssl_size < 0) {
    gpr_log(GPR_ERROR, "Could not write to memory BIO.");
    impl->result = TSI_INTERNAL_ERROR;
    return impl->result;
  }
  *bytes_size = (size_t)bytes_written_into_ssl_size;

  if (!tsi_handshaker_is_in_progress(self)) {
    impl->result = TSI_OK;
    return impl->result;
  } else {
    /* Get ready to get some bytes from SSL. */
    int ssl_result = SSL_do_handshake(impl->ssl);
    ssl_result = SSL_get_error(impl->ssl, ssl_result);
    switch (ssl_result) {
      case SSL_ERROR_WANT_READ:
        if (BIO_pending(impl->from_ssl) == 0) {
          /* We need more data. */
          return TSI_INCOMPLETE_DATA;
        } else {
          return TSI_OK;
        }
      case SSL_ERROR_NONE:
        return TSI_OK;
      default: {
        char err_str[256];
        ERR_error_string_n(ERR_get_error(), err_str, sizeof(err_str));
        gpr_log(GPR_ERROR, "Handshake failed with fatal error %s: %s.",
                ssl_error_string(ssl_result), err_str);
        impl->result = TSI_PROTOCOL_FAILURE;
        return impl->result;
      }
    }
  }
}

static tsi_result ssl_handshaker_extract_peer(tsi_handshaker *self,
                                              tsi_peer *peer) {
  tsi_result result = TSI_OK;
  const unsigned char *alpn_selected = NULL;
  unsigned int alpn_selected_len;
  tsi_ssl_handshaker *impl = (tsi_ssl_handshaker *)self;
  X509 *peer_cert = SSL_get_peer_certificate(impl->ssl);
  if (peer_cert != NULL) {
    result = peer_from_x509(peer_cert, 1, peer);
    X509_free(peer_cert);
    if (result != TSI_OK) return result;
  }
#if TSI_OPENSSL_ALPN_SUPPORT
  SSL_get0_alpn_selected(impl->ssl, &alpn_selected, &alpn_selected_len);
#endif /* TSI_OPENSSL_ALPN_SUPPORT */
  if (alpn_selected == NULL) {
    /* Try npn. */
    SSL_get0_next_proto_negotiated(impl->ssl, &alpn_selected,
                                   &alpn_selected_len);
  }
  if (alpn_selected != NULL) {
    size_t i;
    tsi_peer_property *new_properties =
        gpr_zalloc(sizeof(*new_properties) * (peer->property_count + 1));
    for (i = 0; i < peer->property_count; i++) {
      new_properties[i] = peer->properties[i];
    }
    result = tsi_construct_string_peer_property(
        TSI_SSL_ALPN_SELECTED_PROTOCOL, (const char *)alpn_selected,
        alpn_selected_len, &new_properties[peer->property_count]);
    if (result != TSI_OK) {
      gpr_free(new_properties);
      return result;
    }
    if (peer->properties != NULL) gpr_free(peer->properties);
    peer->property_count++;
    peer->properties = new_properties;
  }
  return result;
}

static tsi_result ssl_handshaker_create_frame_protector(
    tsi_handshaker *self, size_t *max_output_protected_frame_size,
    tsi_frame_protector **protector) {
  size_t actual_max_output_protected_frame_size =
      TSI_SSL_MAX_PROTECTED_FRAME_SIZE_UPPER_BOUND;
  tsi_ssl_handshaker *impl = (tsi_ssl_handshaker *)self;
  tsi_ssl_frame_protector *protector_impl = gpr_zalloc(sizeof(*protector_impl));

  if (max_output_protected_frame_size != NULL) {
    if (*max_output_protected_frame_size >
        TSI_SSL_MAX_PROTECTED_FRAME_SIZE_UPPER_BOUND) {
      *max_output_protected_frame_size =
          TSI_SSL_MAX_PROTECTED_FRAME_SIZE_UPPER_BOUND;
    } else if (*max_output_protected_frame_size <
               TSI_SSL_MAX_PROTECTED_FRAME_SIZE_LOWER_BOUND) {
      *max_output_protected_frame_size =
          TSI_SSL_MAX_PROTECTED_FRAME_SIZE_LOWER_BOUND;
    }
    actual_max_output_protected_frame_size = *max_output_protected_frame_size;
  }
  protector_impl->buffer_size =
      actual_max_output_protected_frame_size - TSI_SSL_MAX_PROTECTION_OVERHEAD;
  protector_impl->buffer = gpr_malloc(protector_impl->buffer_size);
  if (protector_impl->buffer == NULL) {
    gpr_log(GPR_ERROR,
            "Could not allocated buffer for tsi_ssl_frame_protector.");
    gpr_free(protector_impl);
    return TSI_INTERNAL_ERROR;
  }

  /* Transfer ownership of ssl to the frame protector. It is OK as the caller
   * cannot call anything else but destroy on the handshaker after this call. */
  protector_impl->ssl = impl->ssl;
  impl->ssl = NULL;
  protector_impl->into_ssl = impl->into_ssl;
  protector_impl->from_ssl = impl->from_ssl;

  protector_impl->base.vtable = &frame_protector_vtable;
  *protector = &protector_impl->base;
  return TSI_OK;
}

static void ssl_handshaker_destroy(tsi_handshaker *self) {
  tsi_ssl_handshaker *impl = (tsi_ssl_handshaker *)self;
  SSL_free(impl->ssl); /* The BIO objects are owned by ssl */
  gpr_free(impl);
}

static const tsi_handshaker_vtable handshaker_vtable = {
    ssl_handshaker_get_bytes_to_send_to_peer,
    ssl_handshaker_process_bytes_from_peer,
    ssl_handshaker_get_result,
    ssl_handshaker_extract_peer,
    ssl_handshaker_create_frame_protector,
    ssl_handshaker_destroy,
};

/* --- tsi_ssl_handshaker_factory common methods. --- */

tsi_result tsi_ssl_handshaker_factory_create_handshaker(
    tsi_ssl_handshaker_factory *self, const char *server_name_indication,
    tsi_handshaker **handshaker) {
  if (self == NULL || handshaker == NULL) return TSI_INVALID_ARGUMENT;
  return self->create_handshaker(self, server_name_indication, handshaker);
}

void tsi_ssl_handshaker_factory_destroy(tsi_ssl_handshaker_factory *self) {
  if (self == NULL) return;
  self->destroy(self);
}

static tsi_result create_tsi_ssl_handshaker(SSL_CTX *ctx, int is_client,
                                            const char *server_name_indication,
                                            tsi_handshaker **handshaker) {
  SSL *ssl = SSL_new(ctx);
  BIO *into_ssl = NULL;
  BIO *from_ssl = NULL;
  tsi_ssl_handshaker *impl = NULL;
  *handshaker = NULL;
  if (ctx == NULL) {
    gpr_log(GPR_ERROR, "SSL Context is null. Should never happen.");
    return TSI_INTERNAL_ERROR;
  }
  if (ssl == NULL) {
    return TSI_OUT_OF_RESOURCES;
  }
  SSL_set_info_callback(ssl, ssl_info_callback);

  into_ssl = BIO_new(BIO_s_mem());
  from_ssl = BIO_new(BIO_s_mem());
  if (into_ssl == NULL || from_ssl == NULL) {
    gpr_log(GPR_ERROR, "BIO_new failed.");
    SSL_free(ssl);
    if (into_ssl != NULL) BIO_free(into_ssl);
    if (from_ssl != NULL) BIO_free(into_ssl);
    return TSI_OUT_OF_RESOURCES;
  }
  SSL_set_bio(ssl, into_ssl, from_ssl);
  if (is_client) {
    int ssl_result;
    SSL_set_connect_state(ssl);
    if (server_name_indication != NULL) {
      if (!SSL_set_tlsext_host_name(ssl, server_name_indication)) {
        gpr_log(GPR_ERROR, "Invalid server name indication %s.",
                server_name_indication);
        SSL_free(ssl);
        return TSI_INTERNAL_ERROR;
      }
    }
    ssl_result = SSL_do_handshake(ssl);
    ssl_result = SSL_get_error(ssl, ssl_result);
    if (ssl_result != SSL_ERROR_WANT_READ) {
      gpr_log(GPR_ERROR,
              "Unexpected error received from first SSL_do_handshake call: %s",
              ssl_error_string(ssl_result));
      SSL_free(ssl);
      return TSI_INTERNAL_ERROR;
    }
  } else {
    SSL_set_accept_state(ssl);
  }

  impl = gpr_zalloc(sizeof(*impl));
  impl->ssl = ssl;
  impl->into_ssl = into_ssl;
  impl->from_ssl = from_ssl;
  impl->result = TSI_HANDSHAKE_IN_PROGRESS;
  impl->base.vtable = &handshaker_vtable;
  *handshaker = &impl->base;
  return TSI_OK;
}

static int select_protocol_list(const unsigned char **out,
                                unsigned char *outlen,
                                const unsigned char *client_list,
                                size_t client_list_len,
                                const unsigned char *server_list,
                                size_t server_list_len) {
  const unsigned char *client_current = client_list;
  while ((unsigned int)(client_current - client_list) < client_list_len) {
    unsigned char client_current_len = *(client_current++);
    const unsigned char *server_current = server_list;
    while ((server_current >= server_list) &&
           (uintptr_t)(server_current - server_list) < server_list_len) {
      unsigned char server_current_len = *(server_current++);
      if ((client_current_len == server_current_len) &&
          !memcmp(client_current, server_current, server_current_len)) {
        *out = server_current;
        *outlen = server_current_len;
        return SSL_TLSEXT_ERR_OK;
      }
      server_current += server_current_len;
    }
    client_current += client_current_len;
  }
  return SSL_TLSEXT_ERR_NOACK;
}

/* --- tsi_ssl__client_handshaker_factory methods implementation. --- */

static tsi_result ssl_client_handshaker_factory_create_handshaker(
    tsi_ssl_handshaker_factory *self, const char *server_name_indication,
    tsi_handshaker **handshaker) {
  tsi_ssl_client_handshaker_factory *impl =
      (tsi_ssl_client_handshaker_factory *)self;
  return create_tsi_ssl_handshaker(impl->ssl_context, 1, server_name_indication,
                                   handshaker);
}

static void ssl_client_handshaker_factory_destroy(
    tsi_ssl_handshaker_factory *self) {
  tsi_ssl_client_handshaker_factory *impl =
      (tsi_ssl_client_handshaker_factory *)self;
  if (impl->ssl_context != NULL) SSL_CTX_free(impl->ssl_context);
  if (impl->alpn_protocol_list != NULL) gpr_free(impl->alpn_protocol_list);
  gpr_free(impl);
}

static int client_handshaker_factory_npn_callback(SSL *ssl, unsigned char **out,
                                                  unsigned char *outlen,
                                                  const unsigned char *in,
                                                  unsigned int inlen,
                                                  void *arg) {
  tsi_ssl_client_handshaker_factory *factory =
      (tsi_ssl_client_handshaker_factory *)arg;
  return select_protocol_list((const unsigned char **)out, outlen,
                              factory->alpn_protocol_list,
                              factory->alpn_protocol_list_length, in, inlen);
}

/* --- tsi_ssl_server_handshaker_factory methods implementation. --- */

static tsi_result ssl_server_handshaker_factory_create_handshaker(
    tsi_ssl_handshaker_factory *self, const char *server_name_indication,
    tsi_handshaker **handshaker) {
  tsi_ssl_server_handshaker_factory *impl =
      (tsi_ssl_server_handshaker_factory *)self;
  if (impl->ssl_context_count == 0 || server_name_indication != NULL) {
    return TSI_INVALID_ARGUMENT;
  }
  /* Create the handshaker with the first context. We will switch if needed
     because of SNI in ssl_server_handshaker_factory_servername_callback.  */
  return create_tsi_ssl_handshaker(impl->ssl_contexts[0], 0, NULL, handshaker);
}

static void ssl_server_handshaker_factory_destroy(
    tsi_ssl_handshaker_factory *self) {
  tsi_ssl_server_handshaker_factory *impl =
      (tsi_ssl_server_handshaker_factory *)self;
  size_t i;
  for (i = 0; i < impl->ssl_context_count; i++) {
    if (impl->ssl_contexts[i] != NULL) {
      SSL_CTX_free(impl->ssl_contexts[i]);
      tsi_peer_destruct(&impl->ssl_context_x509_subject_names[i]);
    }
  }
  if (impl->ssl_contexts != NULL) gpr_free(impl->ssl_contexts);
  if (impl->ssl_context_x509_subject_names != NULL) {
    gpr_free(impl->ssl_context_x509_subject_names);
  }
  if (impl->alpn_protocol_list != NULL) gpr_free(impl->alpn_protocol_list);
  gpr_free(impl);
}

static int does_entry_match_name(const char *entry, size_t entry_length,
                                 const char *name) {
  const char *dot;
  const char *name_subdomain = NULL;
  size_t name_length = strlen(name);
  size_t name_subdomain_length;
  if (entry_length == 0) return 0;

  /* Take care of '.' terminations. */
  if (name[name_length - 1] == '.') {
    name_length--;
  }
  if (entry[entry_length - 1] == '.') {
    entry_length--;
    if (entry_length == 0) return 0;
  }

  if ((name_length == entry_length) &&
      strncmp(name, entry, entry_length) == 0) {
    return 1; /* Perfect match. */
  }
  if (entry[0] != '*') return 0;

  /* Wildchar subdomain matching. */
  if (entry_length < 3 || entry[1] != '.') { /* At least *.x */
    gpr_log(GPR_ERROR, "Invalid wildchar entry.");
    return 0;
  }
  name_subdomain = strchr(name, '.');
  if (name_subdomain == NULL) return 0;
  name_subdomain_length = strlen(name_subdomain);
  if (name_subdomain_length < 2) return 0;
  name_subdomain++; /* Starts after the dot. */
  name_subdomain_length--;
  entry += 2; /* Remove *. */
  entry_length -= 2;
  dot = strchr(name_subdomain, '.');
  if ((dot == NULL) || (dot == &name_subdomain[name_subdomain_length - 1])) {
    gpr_log(GPR_ERROR, "Invalid toplevel subdomain: %s", name_subdomain);
    return 0;
  }
  if (name_subdomain[name_subdomain_length - 1] == '.') {
    name_subdomain_length--;
  }
  return ((entry_length > 0) && (name_subdomain_length == entry_length) &&
          strncmp(entry, name_subdomain, entry_length) == 0);
}

static int ssl_server_handshaker_factory_servername_callback(SSL *ssl, int *ap,
                                                             void *arg) {
  tsi_ssl_server_handshaker_factory *impl =
      (tsi_ssl_server_handshaker_factory *)arg;
  size_t i = 0;
  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (servername == NULL || strlen(servername) == 0) {
    return SSL_TLSEXT_ERR_NOACK;
  }

  for (i = 0; i < impl->ssl_context_count; i++) {
    if (tsi_ssl_peer_matches_name(&impl->ssl_context_x509_subject_names[i],
                                  servername)) {
      SSL_set_SSL_CTX(ssl, impl->ssl_contexts[i]);
      return SSL_TLSEXT_ERR_OK;
    }
  }
  gpr_log(GPR_ERROR, "No match found for server name: %s.", servername);
  return SSL_TLSEXT_ERR_ALERT_WARNING;
}

#if TSI_OPENSSL_ALPN_SUPPORT
static int server_handshaker_factory_alpn_callback(
    SSL *ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {
  tsi_ssl_server_handshaker_factory *factory =
      (tsi_ssl_server_handshaker_factory *)arg;
  return select_protocol_list(out, outlen, in, inlen,
                              factory->alpn_protocol_list,
                              factory->alpn_protocol_list_length);
}
#endif /* TSI_OPENSSL_ALPN_SUPPORT */

static int server_handshaker_factory_npn_advertised_callback(
    SSL *ssl, const unsigned char **out, unsigned int *outlen, void *arg) {
  tsi_ssl_server_handshaker_factory *factory =
      (tsi_ssl_server_handshaker_factory *)arg;
  *out = factory->alpn_protocol_list;
  GPR_ASSERT(factory->alpn_protocol_list_length <= UINT_MAX);
  *outlen = (unsigned int)factory->alpn_protocol_list_length;
  return SSL_TLSEXT_ERR_OK;
}

/* --- tsi_ssl_handshaker_factory constructors. --- */

tsi_result tsi_create_ssl_client_handshaker_factory(
    const unsigned char *pem_private_key, size_t pem_private_key_size,
    const unsigned char *pem_cert_chain, size_t pem_cert_chain_size,
    const unsigned char *pem_root_certs, size_t pem_root_certs_size,
    const char *cipher_list, const unsigned char **alpn_protocols,
    const unsigned char *alpn_protocols_lengths, uint16_t num_alpn_protocols,
    tsi_ssl_handshaker_factory **factory) {
  SSL_CTX *ssl_context = NULL;
  tsi_ssl_client_handshaker_factory *impl = NULL;
  tsi_result result = TSI_OK;

  gpr_once_init(&init_openssl_once, init_openssl);

  if (factory == NULL) return TSI_INVALID_ARGUMENT;
  *factory = NULL;
  if (pem_root_certs == NULL) return TSI_INVALID_ARGUMENT;

  ssl_context = SSL_CTX_new(TLSv1_2_method());
  if (ssl_context == NULL) {
    gpr_log(GPR_ERROR, "Could not create ssl context.");
    return TSI_INVALID_ARGUMENT;
  }

  impl = gpr_zalloc(sizeof(*impl));
  impl->ssl_context = ssl_context;

  do {
    result =
        populate_ssl_context(ssl_context, pem_private_key, pem_private_key_size,
                             pem_cert_chain, pem_cert_chain_size, cipher_list);
    if (result != TSI_OK) break;
    result = ssl_ctx_load_verification_certs(ssl_context, pem_root_certs,
                                             pem_root_certs_size, NULL);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Cannot load server root certificates.");
      break;
    }

    if (num_alpn_protocols != 0) {
      result = build_alpn_protocol_name_list(
          alpn_protocols, alpn_protocols_lengths, num_alpn_protocols,
          &impl->alpn_protocol_list, &impl->alpn_protocol_list_length);
      if (result != TSI_OK) {
        gpr_log(GPR_ERROR, "Building alpn list failed with error %s.",
                tsi_result_to_string(result));
        break;
      }
#if TSI_OPENSSL_ALPN_SUPPORT
      GPR_ASSERT(impl->alpn_protocol_list_length < UINT_MAX);
      if (SSL_CTX_set_alpn_protos(
              ssl_context, impl->alpn_protocol_list,
              (unsigned int)impl->alpn_protocol_list_length)) {
        gpr_log(GPR_ERROR, "Could not set alpn protocol list to context.");
        result = TSI_INVALID_ARGUMENT;
        break;
      }
#endif /* TSI_OPENSSL_ALPN_SUPPORT */
      SSL_CTX_set_next_proto_select_cb(
          ssl_context, client_handshaker_factory_npn_callback, impl);
    }
  } while (0);
  if (result != TSI_OK) {
    ssl_client_handshaker_factory_destroy(&impl->base);
    return result;
  }
  SSL_CTX_set_verify(ssl_context, SSL_VERIFY_PEER, NULL);
  /* TODO(jboeuf): Add revocation verification. */

  impl->base.create_handshaker =
      ssl_client_handshaker_factory_create_handshaker;
  impl->base.destroy = ssl_client_handshaker_factory_destroy;
  *factory = &impl->base;
  return TSI_OK;
}

tsi_result tsi_create_ssl_server_handshaker_factory(
    const unsigned char **pem_private_keys,
    const size_t *pem_private_keys_sizes, const unsigned char **pem_cert_chains,
    const size_t *pem_cert_chains_sizes, size_t key_cert_pair_count,
    const unsigned char *pem_client_root_certs,
    size_t pem_client_root_certs_size, int force_client_auth,
    const char *cipher_list, const unsigned char **alpn_protocols,
    const unsigned char *alpn_protocols_lengths, uint16_t num_alpn_protocols,
    tsi_ssl_handshaker_factory **factory) {
  return tsi_create_ssl_server_handshaker_factory_ex(
      pem_private_keys, pem_private_keys_sizes, pem_cert_chains,
      pem_cert_chains_sizes, key_cert_pair_count, pem_client_root_certs,
      pem_client_root_certs_size,
      force_client_auth ? TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
                        : TSI_DONT_REQUEST_CLIENT_CERTIFICATE,
      cipher_list, alpn_protocols, alpn_protocols_lengths, num_alpn_protocols,
      factory);
}

tsi_result tsi_create_ssl_server_handshaker_factory_ex(
    const unsigned char **pem_private_keys,
    const size_t *pem_private_keys_sizes, const unsigned char **pem_cert_chains,
    const size_t *pem_cert_chains_sizes, size_t key_cert_pair_count,
    const unsigned char *pem_client_root_certs,
    size_t pem_client_root_certs_size,
    tsi_client_certificate_request_type client_certificate_request,
    const char *cipher_list, const unsigned char **alpn_protocols,
    const unsigned char *alpn_protocols_lengths, uint16_t num_alpn_protocols,
    tsi_ssl_handshaker_factory **factory) {
  tsi_ssl_server_handshaker_factory *impl = NULL;
  tsi_result result = TSI_OK;
  size_t i = 0;

  gpr_once_init(&init_openssl_once, init_openssl);

  if (factory == NULL) return TSI_INVALID_ARGUMENT;
  *factory = NULL;
  if (key_cert_pair_count == 0 || pem_private_keys == NULL ||
      pem_cert_chains == NULL) {
    return TSI_INVALID_ARGUMENT;
  }

  impl = gpr_zalloc(sizeof(*impl));
  impl->base.create_handshaker =
      ssl_server_handshaker_factory_create_handshaker;
  impl->base.destroy = ssl_server_handshaker_factory_destroy;
  impl->ssl_contexts = gpr_zalloc(key_cert_pair_count * sizeof(SSL_CTX *));
  impl->ssl_context_x509_subject_names =
      gpr_zalloc(key_cert_pair_count * sizeof(tsi_peer));
  if (impl->ssl_contexts == NULL ||
      impl->ssl_context_x509_subject_names == NULL) {
    tsi_ssl_handshaker_factory_destroy(&impl->base);
    return TSI_OUT_OF_RESOURCES;
  }
  impl->ssl_context_count = key_cert_pair_count;

  if (num_alpn_protocols > 0) {
    result = build_alpn_protocol_name_list(
        alpn_protocols, alpn_protocols_lengths, num_alpn_protocols,
        &impl->alpn_protocol_list, &impl->alpn_protocol_list_length);
    if (result != TSI_OK) {
      tsi_ssl_handshaker_factory_destroy(&impl->base);
      return result;
    }
  }

  for (i = 0; i < key_cert_pair_count; i++) {
    do {
      impl->ssl_contexts[i] = SSL_CTX_new(TLSv1_2_method());
      if (impl->ssl_contexts[i] == NULL) {
        gpr_log(GPR_ERROR, "Could not create ssl context.");
        result = TSI_OUT_OF_RESOURCES;
        break;
      }
      result = populate_ssl_context(
          impl->ssl_contexts[i], pem_private_keys[i], pem_private_keys_sizes[i],
          pem_cert_chains[i], pem_cert_chains_sizes[i], cipher_list);
      if (result != TSI_OK) break;

      if (pem_client_root_certs != NULL) {
        STACK_OF(X509_NAME) *root_names = NULL;
        result = ssl_ctx_load_verification_certs(
            impl->ssl_contexts[i], pem_client_root_certs,
            pem_client_root_certs_size, &root_names);
        if (result != TSI_OK) {
          gpr_log(GPR_ERROR, "Invalid verification certs.");
          break;
        }
        SSL_CTX_set_client_CA_list(impl->ssl_contexts[i], root_names);
        switch (client_certificate_request) {
          case TSI_DONT_REQUEST_CLIENT_CERTIFICATE:
            SSL_CTX_set_verify(impl->ssl_contexts[i], SSL_VERIFY_NONE, NULL);
            break;
          case TSI_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY:
            SSL_CTX_set_verify(impl->ssl_contexts[i], SSL_VERIFY_PEER,
                               NullVerifyCallback);
            break;
          case TSI_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY:
            SSL_CTX_set_verify(impl->ssl_contexts[i], SSL_VERIFY_PEER, NULL);
            break;
          case TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY:
            SSL_CTX_set_verify(
                impl->ssl_contexts[i],
                SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                NullVerifyCallback);
            break;
          case TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY:
            SSL_CTX_set_verify(
                impl->ssl_contexts[i],
                SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
            break;
        }
        /* TODO(jboeuf): Add revocation verification. */
      }

      result = extract_x509_subject_names_from_pem_cert(
          pem_cert_chains[i], pem_cert_chains_sizes[i],
          &impl->ssl_context_x509_subject_names[i]);
      if (result != TSI_OK) break;

      SSL_CTX_set_tlsext_servername_callback(
          impl->ssl_contexts[i],
          ssl_server_handshaker_factory_servername_callback);
      SSL_CTX_set_tlsext_servername_arg(impl->ssl_contexts[i], impl);
#if TSI_OPENSSL_ALPN_SUPPORT
      SSL_CTX_set_alpn_select_cb(impl->ssl_contexts[i],
                                 server_handshaker_factory_alpn_callback, impl);
#endif /* TSI_OPENSSL_ALPN_SUPPORT */
      SSL_CTX_set_next_protos_advertised_cb(
          impl->ssl_contexts[i],
          server_handshaker_factory_npn_advertised_callback, impl);
    } while (0);

    if (result != TSI_OK) {
      tsi_ssl_handshaker_factory_destroy(&impl->base);
      return result;
    }
  }
  *factory = &impl->base;
  return TSI_OK;
}

/* --- tsi_ssl utils. --- */

int tsi_ssl_peer_matches_name(const tsi_peer *peer, const char *name) {
  size_t i = 0;
  size_t san_count = 0;
  const tsi_peer_property *cn_property = NULL;
  int like_ip = looks_like_ip_address(name);

  /* Check the SAN first. */
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property *property = &peer->properties[i];
    if (property->name == NULL) continue;
    if (strcmp(property->name,
               TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) == 0) {
      san_count++;

      if (!like_ip && does_entry_match_name(property->value.data,
                                            property->value.length, name)) {
        return 1;
      } else if (like_ip &&
                 strncmp(name, property->value.data, property->value.length) ==
                     0 &&
                 strlen(name) == property->value.length) {
        /* IP Addresses are exact matches only. */
        return 1;
      }
    } else if (strcmp(property->name,
                      TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY) == 0) {
      cn_property = property;
    }
  }

  /* If there's no SAN, try the CN, but only if its not like an IP Address */
  if (san_count == 0 && cn_property != NULL && !like_ip) {
    if (does_entry_match_name(cn_property->value.data,
                              cn_property->value.length, name)) {
      return 1;
    }
  }

  return 0; /* Not found. */
}
