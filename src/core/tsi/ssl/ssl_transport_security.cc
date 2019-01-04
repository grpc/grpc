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

#include <grpc/support/port_platform.h>

#include "src/core/tsi/grpc_shadow_boringssl.h"

#include "src/core/tsi/ssl/ssl_transport_security.h"

#include <limits.h>
#include <string.h>

/* TODO(jboeuf): refactor inet_ntop into a portability header. */
/* Note: for whomever reads this and tries to refactor this, this
   can't be in grpc, it has to be in gpr. */
#ifdef GPR_WINDOWS
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd_id.h>

extern "C" {
#include <openssl/bio.h>
#include <openssl/crypto.h> /* For OPENSSL_free */
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
}

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/security/credentials/tls/grpc_tls_credentials_options.h"
#include "src/core/tsi/ssl/session_cache/ssl_session_cache.h"
#include "src/core/tsi/ssl/ssl_transport_security_util.h"
#include "src/core/tsi/ssl_types.h"
#include "src/core/tsi/transport_security.h"

/* --- Constants. ---*/

#define TSI_SSL_MAX_PROTECTED_FRAME_SIZE_UPPER_BOUND 16384
#define TSI_SSL_MAX_PROTECTED_FRAME_SIZE_LOWER_BOUND 1024
#define TSI_SSL_HANDSHAKER_OUTGOING_BUFFER_INITIAL_SIZE 1024

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

struct tsi_ssl_root_certs_store {
  X509_STORE* store;
};

struct tsi_ssl_handshaker_factory {
  const tsi_ssl_handshaker_factory_vtable* vtable;
  gpr_refcount refcount;
};

struct tsi_ssl_client_handshaker_factory {
  tsi_ssl_handshaker_factory base;
  SSL_CTX* ssl_context;
  unsigned char* alpn_protocol_list;
  size_t alpn_protocol_list_length;
  grpc_core::RefCountedPtr<tsi::SslSessionLRUCache> session_cache;
};

struct tsi_ssl_server_handshaker_factory {
  /* Several contexts to support SNI.
     The tsi_peer array contains the subject names of the server certificates
     associated with the contexts at the same index.  */
  tsi_ssl_handshaker_factory base;
  SSL_CTX** ssl_contexts;
  tsi_peer* ssl_context_x509_subject_names;
  size_t ssl_context_count;
  unsigned char* alpn_protocol_list;
  size_t alpn_protocol_list_length;
  tsi_client_certificate_request_type client_certificate_request;
};

/* Struct holding pointers to different arguments of tsi_handshaker_next.
 * It does not own any of its fields. */
struct tsi_handshaker_next_arg {
  const unsigned char* received_bytes;
  size_t received_bytes_size;
  tsi_handshaker_on_next_done_cb cb;
  void* user_data;
};

typedef struct {
  tsi_handshaker base;
  SSL* ssl;
  BIO* network_io;
  tsi_result result;
  unsigned char* outgoing_bytes_buffer;
  size_t outgoing_bytes_buffer_size;
  tsi_ssl_handshaker_factory* factory_ref;
  bool is_client;
  tsi_ssl_session_cache* session_cache;
  const char* session_ticket_key;
  size_t session_ticket_key_size;
  const char** alpn_protocols;
  size_t num_alpn_protocols;
  const char* pem_root_certs;
  const tsi_ssl_root_certs_store* root_store;
  grpc_tls_credentials_options* options;
  grpc_tls_credential_reload_arg* reload_arg;
  const char* server_name_indication;
  bool credential_reloaded;
  tsi_handshaker_next_arg handshaker_next_arg;
  grpc_closure on_credential_reloaded;
  tsi_ssl_pem_key_cert_pair* key_cert_pairs;
  size_t num_key_cert_pairs;
} tsi_ssl_handshaker;

typedef struct {
  tsi_handshaker_result base;
  SSL* ssl;
  BIO* network_io;
  unsigned char* unused_bytes;
  size_t unused_bytes_size;
  bool is_client;
} tsi_ssl_handshaker_result;

typedef struct {
  tsi_frame_protector base;
  SSL* ssl;
  BIO* network_io;
  unsigned char* buffer;
  size_t buffer_size;
  size_t buffer_offset;
} tsi_ssl_frame_protector;

/* Forward declarations. */

/* --- Util functions to get peer certificate chain. --- */
/*static char* get_cert_ASN1(X509* cert, int* len);
static char* get_cert_chain_ASN1_client(STACK_OF(X509) * cert_chain);
static char* get_cert_chain_ASN1_server(X509* leaf_cert, STACK_OF(X509) *
cert_chain);
*/

/* --- Util functions to update SSL TSI handshakers. --- */
static tsi_result update_ssl_handshaker(tsi_ssl_handshaker* handshaker,
                                        grpc_error* error);
static tsi_result populate_ssl_handshaker(
    tsi_ssl_handshaker* handshaker, SSL_CTX* ctx, int is_client,
    const char* server_name_indication, tsi_ssl_handshaker_factory* factory,
    tsi::SslSessionLRUCache* session_cache);

/* --- gRPC-provided callback functions for credential reload. --- */
static void on_credential_reloaded(void* arg, grpc_error* error);
static void credential_reload_done_cb(grpc_tls_credential_reload_arg* arg);
static grpc_error* process_credential_reload_result(
    grpc_tls_credential_reload_arg* arg);

/* --- Library Initialization. ---*/

static gpr_once g_init_openssl_once = GPR_ONCE_INIT;
static int g_ssl_ctx_ex_factory_index = -1;
static const unsigned char kSslSessionIdContext[] = {'g', 'r', 'p', 'c'};

#if OPENSSL_VERSION_NUMBER < 0x10100000
static gpr_mu* g_openssl_mutexes = nullptr;
static void openssl_locking_cb(int mode, int type, const char* file,
                               int line) GRPC_UNUSED;
static unsigned long openssl_thread_id_cb(void) GRPC_UNUSED;

static void openssl_locking_cb(int mode, int type, const char* file, int line) {
  if (mode & CRYPTO_LOCK) {
    gpr_mu_lock(&g_openssl_mutexes[type]);
  } else {
    gpr_mu_unlock(&g_openssl_mutexes[type]);
  }
}

static unsigned long openssl_thread_id_cb(void) {
  return static_cast<unsigned long>(gpr_thd_currentid());
}
#endif

static void init_openssl(void) {
#if OPENSSL_API_COMPAT >= 0x10100000L
  OPENSSL_init_ssl(0, NULL);
#else
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000
  if (!CRYPTO_get_locking_callback()) {
    int num_locks = CRYPTO_num_locks();
    GPR_ASSERT(num_locks > 0);
    g_openssl_mutexes = static_cast<gpr_mu*>(
        gpr_malloc(static_cast<size_t>(num_locks) * sizeof(gpr_mu)));
    for (int i = 0; i < num_locks; i++) {
      gpr_mu_init(&g_openssl_mutexes[i]);
    }
    CRYPTO_set_locking_callback(openssl_locking_cb);
    CRYPTO_set_id_callback(openssl_thread_id_cb);
  } else {
    gpr_log(GPR_INFO, "OpenSSL callback has already been set.");
  }
#endif
  g_ssl_ctx_ex_factory_index =
      SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  GPR_ASSERT(g_ssl_ctx_ex_factory_index != -1);
}

/* --- Ssl utils. ---*/

static const char* ssl_error_string(int error) {
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
static void ssl_log_where_info(const SSL* ssl, int where, int flag,
                               const char* msg) {
  if ((where & flag) && tsi_tracing_enabled.enabled()) {
    gpr_log(GPR_INFO, "%20.20s - %30.30s  - %5.10s", msg,
            SSL_state_string_long(ssl), SSL_state_string(ssl));
  }
}

/* Used for debugging. TODO(jboeuf): Remove when code is mature enough. */
static void ssl_info_callback(const SSL* ssl, int where, int ret) {
  if (ret == 0) {
    gpr_log(GPR_ERROR, "ssl_info_callback: error occurred.\n");
    return;
  }

  ssl_log_where_info(ssl, where, SSL_CB_LOOP, "LOOP");
  ssl_log_where_info(ssl, where, SSL_CB_HANDSHAKE_START, "HANDSHAKE START");
  ssl_log_where_info(ssl, where, SSL_CB_HANDSHAKE_DONE, "HANDSHAKE DONE");
}

/* Returns 1 if name looks like an IP address, 0 otherwise.
   This is a very rough heuristic, and only handles IPv6 in hexadecimal form. */
static int looks_like_ip_address(const char* name) {
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
static tsi_result ssl_get_x509_common_name(X509* cert, unsigned char** utf8,
                                           size_t* utf8_size) {
  int common_name_index = -1;
  X509_NAME_ENTRY* common_name_entry = nullptr;
  ASN1_STRING* common_name_asn1 = nullptr;
  X509_NAME* subject_name = X509_get_subject_name(cert);
  int utf8_returned_size = 0;
  if (subject_name == nullptr) {
    gpr_log(GPR_INFO, "Could not get subject name from certificate.");
    return TSI_NOT_FOUND;
  }
  common_name_index =
      X509_NAME_get_index_by_NID(subject_name, NID_commonName, -1);
  if (common_name_index == -1) {
    gpr_log(GPR_INFO, "Could not get common name of subject from certificate.");
    return TSI_NOT_FOUND;
  }
  common_name_entry = X509_NAME_get_entry(subject_name, common_name_index);
  if (common_name_entry == nullptr) {
    gpr_log(GPR_ERROR, "Could not get common name entry from certificate.");
    return TSI_INTERNAL_ERROR;
  }
  common_name_asn1 = X509_NAME_ENTRY_get_data(common_name_entry);
  if (common_name_asn1 == nullptr) {
    gpr_log(GPR_ERROR,
            "Could not get common name entry asn1 from certificate.");
    return TSI_INTERNAL_ERROR;
  }
  utf8_returned_size = ASN1_STRING_to_UTF8(utf8, common_name_asn1);
  if (utf8_returned_size < 0) {
    gpr_log(GPR_ERROR, "Could not extract utf8 from asn1 string.");
    return TSI_OUT_OF_RESOURCES;
  }
  *utf8_size = static_cast<size_t>(utf8_returned_size);
  return TSI_OK;
}

/* Gets the subject CN of an X509 cert as a tsi_peer_property. */
static tsi_result peer_property_from_x509_common_name(
    X509* cert, tsi_peer_property* property) {
  unsigned char* common_name;
  size_t common_name_size;
  tsi_result result =
      ssl_get_x509_common_name(cert, &common_name, &common_name_size);
  if (result != TSI_OK) {
    if (result == TSI_NOT_FOUND) {
      common_name = nullptr;
      common_name_size = 0;
    } else {
      return result;
    }
  }
  result = tsi_construct_string_peer_property(
      TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY,
      common_name == nullptr ? "" : reinterpret_cast<const char*>(common_name),
      common_name_size, property);
  OPENSSL_free(common_name);
  return result;
}

/* Gets the X509 cert in PEM format as a tsi_peer_property. */
static tsi_result add_pem_certificate(X509* cert, tsi_peer_property* property) {
  BIO* bio = BIO_new(BIO_s_mem());
  if (!PEM_write_bio_X509(bio, cert)) {
    BIO_free(bio);
    return TSI_INTERNAL_ERROR;
  }
  char* contents;
  long len = BIO_get_mem_data(bio, &contents);
  if (len <= 0) {
    BIO_free(bio);
    return TSI_INTERNAL_ERROR;
  }
  tsi_result result = tsi_construct_string_peer_property(
      TSI_X509_PEM_CERT_PROPERTY, (const char*)contents,
      static_cast<size_t>(len), property);
  BIO_free(bio);
  return result;
}

/* Gets the subject SANs from an X509 cert as a tsi_peer_property. */
static tsi_result add_subject_alt_names_properties_to_peer(
    tsi_peer* peer, GENERAL_NAMES* subject_alt_names,
    size_t subject_alt_name_count) {
  size_t i;
  tsi_result result = TSI_OK;

  /* Reset for DNS entries filtering. */
  peer->property_count -= subject_alt_name_count;

  for (i = 0; i < subject_alt_name_count; i++) {
    GENERAL_NAME* subject_alt_name =
        sk_GENERAL_NAME_value(subject_alt_names, TSI_SIZE_AS_SIZE(i));
    if (subject_alt_name->type == GEN_DNS ||
        subject_alt_name->type == GEN_EMAIL ||
        subject_alt_name->type == GEN_URI) {
      unsigned char* name = nullptr;
      int name_size;
      if (subject_alt_name->type == GEN_DNS) {
        name_size = ASN1_STRING_to_UTF8(&name, subject_alt_name->d.dNSName);
      } else if (subject_alt_name->type == GEN_EMAIL) {
        name_size = ASN1_STRING_to_UTF8(&name, subject_alt_name->d.rfc822Name);
      } else {
        name_size = ASN1_STRING_to_UTF8(
            &name, subject_alt_name->d.uniformResourceIdentifier);
      }
      if (name_size < 0) {
        gpr_log(GPR_ERROR, "Could not get utf8 from asn1 string.");
        result = TSI_INTERNAL_ERROR;
        break;
      }
      if (subject_alt_name->type == GEN_URI) {
        result = tsi_construct_string_peer_property(
            TSI_X509_SUBJECT_ALTERNATIVE_NAME_URI_PEER_PROPERTY,
            reinterpret_cast<const char*>(name), static_cast<size_t>(name_size),
            &peer->properties[peer->property_count++]);
      }
      result = tsi_construct_string_peer_property(
          TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
          reinterpret_cast<const char*>(name), static_cast<size_t>(name_size),
          &peer->properties[peer->property_count++]);
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
      const char* name = inet_ntop(af, subject_alt_name->d.iPAddress->data,
                                   ntop_buf, INET6_ADDRSTRLEN);
      if (name == nullptr) {
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
static tsi_result peer_from_x509(X509* cert, int include_certificate_type,
                                 tsi_peer* peer) {
  /* TODO(jboeuf): Maybe add more properties. */
  GENERAL_NAMES* subject_alt_names = static_cast<GENERAL_NAMES*>(
      X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
  int subject_alt_name_count =
      (subject_alt_names != nullptr)
          ? static_cast<int>(sk_GENERAL_NAME_num(subject_alt_names))
          : 0;
  size_t property_count;
  tsi_result result;
  GPR_ASSERT(subject_alt_name_count >= 0);
  property_count = (include_certificate_type ? static_cast<size_t>(1) : 0) +
                   2 /* common name, certificate */ +
                   static_cast<size_t>(subject_alt_name_count);
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
          peer, subject_alt_names, static_cast<size_t>(subject_alt_name_count));
      if (result != TSI_OK) break;
    }
  } while (0);

  if (subject_alt_names != nullptr) {
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
    ERR_error_string_n(static_cast<uint32_t>(err), details, sizeof(details));
    gpr_log(GPR_ERROR, "%s", details);
  }
}

/* Performs an SSL_read and handle errors. */
static tsi_result do_ssl_read(SSL* ssl, unsigned char* unprotected_bytes,
                              size_t* unprotected_bytes_size) {
  int read_from_ssl;
  GPR_ASSERT(*unprotected_bytes_size <= INT_MAX);
  read_from_ssl = SSL_read(ssl, unprotected_bytes,
                           static_cast<int>(*unprotected_bytes_size));
  if (read_from_ssl <= 0) {
    read_from_ssl = SSL_get_error(ssl, read_from_ssl);
    switch (read_from_ssl) {
      case SSL_ERROR_ZERO_RETURN: /* Received a close_notify alert. */
      case SSL_ERROR_WANT_READ:   /* We need more data to finish the frame. */
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
  *unprotected_bytes_size = static_cast<size_t>(read_from_ssl);
  return TSI_OK;
}

/* Performs an SSL_write and handle errors. */
static tsi_result do_ssl_write(SSL* ssl, unsigned char* unprotected_bytes,
                               size_t unprotected_bytes_size) {
  int ssl_write_result;
  GPR_ASSERT(unprotected_bytes_size <= INT_MAX);
  ssl_write_result = SSL_write(ssl, unprotected_bytes,
                               static_cast<int>(unprotected_bytes_size));
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
static tsi_result ssl_ctx_use_certificate_chain(SSL_CTX* context,
                                                const char* pem_cert_chain,
                                                size_t pem_cert_chain_size) {
  tsi_result result = TSI_OK;
  X509* certificate = nullptr;
  BIO* pem;
  GPR_ASSERT(pem_cert_chain_size <= INT_MAX);
  pem = BIO_new_mem_buf((void*)pem_cert_chain,
                        static_cast<int>(pem_cert_chain_size));
  if (pem == nullptr) return TSI_OUT_OF_RESOURCES;

  do {
    certificate = PEM_read_bio_X509_AUX(pem, nullptr, nullptr, (void*)"");
    if (certificate == nullptr) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    if (!SSL_CTX_use_certificate(context, certificate)) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    while (1) {
      X509* certificate_authority =
          PEM_read_bio_X509(pem, nullptr, nullptr, (void*)"");
      if (certificate_authority == nullptr) {
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

  if (certificate != nullptr) X509_free(certificate);
  BIO_free(pem);
  return result;
}

/* Loads an in-memory PEM private key into the SSL context. */
static tsi_result ssl_ctx_use_private_key(SSL_CTX* context, const char* pem_key,
                                          size_t pem_key_size) {
  tsi_result result = TSI_OK;
  EVP_PKEY* private_key = nullptr;
  BIO* pem;
  GPR_ASSERT(pem_key_size <= INT_MAX);
  pem = BIO_new_mem_buf((void*)pem_key, static_cast<int>(pem_key_size));
  if (pem == nullptr) return TSI_OUT_OF_RESOURCES;
  do {
    private_key = PEM_read_bio_PrivateKey(pem, nullptr, nullptr, (void*)"");
    if (private_key == nullptr) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    if (!SSL_CTX_use_PrivateKey(context, private_key)) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
  } while (0);
  if (private_key != nullptr) EVP_PKEY_free(private_key);
  BIO_free(pem);
  return result;
}

/* Loads in-memory PEM verification certs into the SSL context and optionally
   returns the verification cert names (root_names can be NULL). */
static tsi_result x509_store_load_certs(X509_STORE* cert_store,
                                        const char* pem_roots,
                                        size_t pem_roots_size,
                                        STACK_OF(X509_NAME) * *root_names) {
  tsi_result result = TSI_OK;
  size_t num_roots = 0;
  X509* root = nullptr;
  X509_NAME* root_name = nullptr;
  BIO* pem;
  GPR_ASSERT(pem_roots_size <= INT_MAX);
  pem = BIO_new_mem_buf((void*)pem_roots, static_cast<int>(pem_roots_size));
  if (cert_store == nullptr) return TSI_INVALID_ARGUMENT;
  if (pem == nullptr) return TSI_OUT_OF_RESOURCES;
  if (root_names != nullptr) {
    *root_names = sk_X509_NAME_new_null();
    if (*root_names == nullptr) return TSI_OUT_OF_RESOURCES;
  }

  while (1) {
    root = PEM_read_bio_X509_AUX(pem, nullptr, nullptr, (void*)"");
    if (root == nullptr) {
      ERR_clear_error();
      break; /* We're at the end of stream. */
    }
    if (root_names != nullptr) {
      root_name = X509_get_subject_name(root);
      if (root_name == nullptr) {
        gpr_log(GPR_ERROR, "Could not get name from root certificate.");
        result = TSI_INVALID_ARGUMENT;
        break;
      }
      root_name = X509_NAME_dup(root_name);
      if (root_name == nullptr) {
        result = TSI_OUT_OF_RESOURCES;
        break;
      }
      sk_X509_NAME_push(*root_names, root_name);
      root_name = nullptr;
    }
    if (!X509_STORE_add_cert(cert_store, root)) {
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
    if (root != nullptr) X509_free(root);
    if (root_names != nullptr) {
      sk_X509_NAME_pop_free(*root_names, X509_NAME_free);
      *root_names = nullptr;
      if (root_name != nullptr) X509_NAME_free(root_name);
    }
  }
  BIO_free(pem);
  return result;
}

static tsi_result ssl_ctx_load_verification_certs(SSL_CTX* context,
                                                  const char* pem_roots,
                                                  size_t pem_roots_size,
                                                  STACK_OF(X509_NAME) *
                                                      *root_name) {
  X509_STORE* cert_store = SSL_CTX_get_cert_store(context);
  return x509_store_load_certs(cert_store, pem_roots, pem_roots_size,
                               root_name);
}

/* Populates the SSL context with a private key and a cert chain, and sets the
   cipher list and the ephemeral ECDH key. */
static tsi_result populate_ssl_context(
    SSL_CTX* context, const tsi_ssl_pem_key_cert_pair* key_cert_pair,
    const char* cipher_list) {
  tsi_result result = TSI_OK;
  if (key_cert_pair != nullptr) {
    if (key_cert_pair->cert_chain != nullptr) {
      result = ssl_ctx_use_certificate_chain(context, key_cert_pair->cert_chain,
                                             strlen(key_cert_pair->cert_chain));
      if (result != TSI_OK) {
        gpr_log(GPR_ERROR, "Invalid cert chain file.");
        return result;
      }
    }
    if (key_cert_pair->private_key != nullptr) {
      result = ssl_ctx_use_private_key(context, key_cert_pair->private_key,
                                       strlen(key_cert_pair->private_key));
      if (result != TSI_OK || !SSL_CTX_check_private_key(context)) {
        gpr_log(GPR_ERROR, "Invalid private key.");
        return result != TSI_OK ? result : TSI_INVALID_ARGUMENT;
      }
    }
  }
  if ((cipher_list != nullptr) &&
      !SSL_CTX_set_cipher_list(context, cipher_list)) {
    gpr_log(GPR_ERROR, "Invalid cipher list: %s.", cipher_list);
    return TSI_INVALID_ARGUMENT;
  }
  {
    EC_KEY* ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
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
    const char* pem_cert, tsi_peer* peer, int include_certificate_type) {
  tsi_result result = TSI_OK;
  X509* cert = nullptr;
  BIO* pem;
  pem = BIO_new_mem_buf((void*)pem_cert, static_cast<int>(strlen(pem_cert)));
  if (pem == nullptr) return TSI_OUT_OF_RESOURCES;
  cert = PEM_read_bio_X509(pem, nullptr, nullptr, (void*)"");
  if (cert == nullptr) {
    gpr_log(GPR_ERROR, "Invalid certificate");
    result = TSI_INVALID_ARGUMENT;
  } else {
    result = peer_from_x509(cert, include_certificate_type, peer);
  }
  if (cert != nullptr) X509_free(cert);
  BIO_free(pem);
  return result;
}

/* Builds the alpn protocol name list according to rfc 7301. */
static tsi_result build_alpn_protocol_name_list(
    const char** alpn_protocols, uint16_t num_alpn_protocols,
    unsigned char** protocol_name_list, size_t* protocol_name_list_length) {
  uint16_t i;
  unsigned char* current;
  *protocol_name_list = nullptr;
  *protocol_name_list_length = 0;
  if (num_alpn_protocols == 0) return TSI_INVALID_ARGUMENT;
  for (i = 0; i < num_alpn_protocols; i++) {
    size_t length =
        alpn_protocols[i] == nullptr ? 0 : strlen(alpn_protocols[i]);
    if (length == 0 || length > 255) {
      gpr_log(GPR_ERROR, "Invalid protocol name length: %d.",
              static_cast<int>(length));
      return TSI_INVALID_ARGUMENT;
    }
    *protocol_name_list_length += length + 1;
  }
  *protocol_name_list =
      static_cast<unsigned char*>(gpr_malloc(*protocol_name_list_length));
  if (*protocol_name_list == nullptr) return TSI_OUT_OF_RESOURCES;
  current = *protocol_name_list;
  for (i = 0; i < num_alpn_protocols; i++) {
    size_t length = strlen(alpn_protocols[i]);
    *(current++) = static_cast<uint8_t>(length); /* max checked above. */
    memcpy(current, alpn_protocols[i], length);
    current += length;
  }
  /* Safety check. */
  if ((current < *protocol_name_list) ||
      (static_cast<uintptr_t>(current - *protocol_name_list) !=
       *protocol_name_list_length)) {
    return TSI_INTERNAL_ERROR;
  }
  return TSI_OK;
}

// The verification callback is used for clients that don't really care about
// the server's certificate, but we need to pull it anyway, in case a higher
// layer wants to look at it. In this case the verification may fail, but
// we don't really care.
static int NullVerifyCallback(int preverify_ok, X509_STORE_CTX* ctx) {
  return 1;
}

/* --- tsi_ssl_root_certs_store methods implementation. ---*/

tsi_ssl_root_certs_store* tsi_ssl_root_certs_store_create(
    const char* pem_roots) {
  if (pem_roots == nullptr) {
    gpr_log(GPR_ERROR, "The root certificates are empty.");
    return nullptr;
  }
  tsi_ssl_root_certs_store* root_store = static_cast<tsi_ssl_root_certs_store*>(
      gpr_zalloc(sizeof(tsi_ssl_root_certs_store)));
  if (root_store == nullptr) {
    gpr_log(GPR_ERROR, "Could not allocate buffer for ssl_root_certs_store.");
    return nullptr;
  }
  root_store->store = X509_STORE_new();
  if (root_store->store == nullptr) {
    gpr_log(GPR_ERROR, "Could not allocate buffer for X509_STORE.");
    gpr_free(root_store);
    return nullptr;
  }
  tsi_result result = x509_store_load_certs(root_store->store, pem_roots,
                                            strlen(pem_roots), nullptr);
  if (result != TSI_OK) {
    gpr_log(GPR_ERROR, "Could not load root certificates.");
    X509_STORE_free(root_store->store);
    gpr_free(root_store);
    return nullptr;
  }
  return root_store;
}

void tsi_ssl_root_certs_store_destroy(tsi_ssl_root_certs_store* self) {
  if (self == nullptr) return;
  X509_STORE_free(self->store);
  gpr_free(self);
}

/* --- tsi_ssl_session_cache methods implementation. ---*/

tsi_ssl_session_cache* tsi_ssl_session_cache_create_lru(size_t capacity) {
  /* Pointer will be dereferenced by unref call. */
  return reinterpret_cast<tsi_ssl_session_cache*>(
      tsi::SslSessionLRUCache::Create(capacity).release());
}

void tsi_ssl_session_cache_ref(tsi_ssl_session_cache* cache) {
  /* Pointer will be dereferenced by unref call. */
  reinterpret_cast<tsi::SslSessionLRUCache*>(cache)->Ref().release();
}

void tsi_ssl_session_cache_unref(tsi_ssl_session_cache* cache) {
  reinterpret_cast<tsi::SslSessionLRUCache*>(cache)->Unref();
}

/* --- tsi_frame_protector methods implementation. ---*/

static tsi_result ssl_protector_protect(tsi_frame_protector* self,
                                        const unsigned char* unprotected_bytes,
                                        size_t* unprotected_bytes_size,
                                        unsigned char* protected_output_frames,
                                        size_t* protected_output_frames_size) {
  tsi_ssl_frame_protector* impl =
      reinterpret_cast<tsi_ssl_frame_protector*>(self);
  int read_from_ssl;
  size_t available;
  tsi_result result = TSI_OK;

  /* First see if we have some pending data in the SSL BIO. */
  int pending_in_ssl = static_cast<int>(BIO_pending(impl->network_io));
  if (pending_in_ssl > 0) {
    *unprotected_bytes_size = 0;
    GPR_ASSERT(*protected_output_frames_size <= INT_MAX);
    read_from_ssl = BIO_read(impl->network_io, protected_output_frames,
                             static_cast<int>(*protected_output_frames_size));
    if (read_from_ssl < 0) {
      gpr_log(GPR_ERROR,
              "Could not read from BIO even though some data is pending");
      return TSI_INTERNAL_ERROR;
    }
    *protected_output_frames_size = static_cast<size_t>(read_from_ssl);
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
  read_from_ssl = BIO_read(impl->network_io, protected_output_frames,
                           static_cast<int>(*protected_output_frames_size));
  if (read_from_ssl < 0) {
    gpr_log(GPR_ERROR, "Could not read from BIO after SSL_write.");
    return TSI_INTERNAL_ERROR;
  }
  *protected_output_frames_size = static_cast<size_t>(read_from_ssl);
  *unprotected_bytes_size = available;
  impl->buffer_offset = 0;
  return TSI_OK;
}

static tsi_result ssl_protector_protect_flush(
    tsi_frame_protector* self, unsigned char* protected_output_frames,
    size_t* protected_output_frames_size, size_t* still_pending_size) {
  tsi_result result = TSI_OK;
  tsi_ssl_frame_protector* impl =
      reinterpret_cast<tsi_ssl_frame_protector*>(self);
  int read_from_ssl = 0;
  int pending;

  if (impl->buffer_offset != 0) {
    result = do_ssl_write(impl->ssl, impl->buffer, impl->buffer_offset);
    if (result != TSI_OK) return result;
    impl->buffer_offset = 0;
  }

  pending = static_cast<int>(BIO_pending(impl->network_io));
  GPR_ASSERT(pending >= 0);
  *still_pending_size = static_cast<size_t>(pending);
  if (*still_pending_size == 0) return TSI_OK;

  GPR_ASSERT(*protected_output_frames_size <= INT_MAX);
  read_from_ssl = BIO_read(impl->network_io, protected_output_frames,
                           static_cast<int>(*protected_output_frames_size));
  if (read_from_ssl <= 0) {
    gpr_log(GPR_ERROR, "Could not read from BIO after SSL_write.");
    return TSI_INTERNAL_ERROR;
  }
  *protected_output_frames_size = static_cast<size_t>(read_from_ssl);
  pending = static_cast<int>(BIO_pending(impl->network_io));
  GPR_ASSERT(pending >= 0);
  *still_pending_size = static_cast<size_t>(pending);
  return TSI_OK;
}

static tsi_result ssl_protector_unprotect(
    tsi_frame_protector* self, const unsigned char* protected_frames_bytes,
    size_t* protected_frames_bytes_size, unsigned char* unprotected_bytes,
    size_t* unprotected_bytes_size) {
  tsi_result result = TSI_OK;
  int written_into_ssl = 0;
  size_t output_bytes_size = *unprotected_bytes_size;
  size_t output_bytes_offset = 0;
  tsi_ssl_frame_protector* impl =
      reinterpret_cast<tsi_ssl_frame_protector*>(self);

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
  written_into_ssl = BIO_write(impl->network_io, protected_frames_bytes,
                               static_cast<int>(*protected_frames_bytes_size));
  if (written_into_ssl < 0) {
    gpr_log(GPR_ERROR, "Sending protected frame to ssl failed with %d",
            written_into_ssl);
    return TSI_INTERNAL_ERROR;
  }
  *protected_frames_bytes_size = static_cast<size_t>(written_into_ssl);

  /* Now try to read some data again. */
  result = do_ssl_read(impl->ssl, unprotected_bytes, unprotected_bytes_size);
  if (result == TSI_OK) {
    /* Don't forget to output the total number of bytes read. */
    *unprotected_bytes_size += output_bytes_offset;
  }
  return result;
}

static void ssl_protector_destroy(tsi_frame_protector* self) {
  tsi_ssl_frame_protector* impl =
      reinterpret_cast<tsi_ssl_frame_protector*>(self);
  if (impl->buffer != nullptr) gpr_free(impl->buffer);
  if (impl->ssl != nullptr) SSL_free(impl->ssl);
  if (impl->network_io != nullptr) BIO_free(impl->network_io);
  gpr_free(self);
}

static const tsi_frame_protector_vtable frame_protector_vtable = {
    ssl_protector_protect,
    ssl_protector_protect_flush,
    ssl_protector_unprotect,
    ssl_protector_destroy,
};

/* --- tsi_server_handshaker_factory methods implementation. --- */

static void tsi_ssl_handshaker_factory_destroy(
    tsi_ssl_handshaker_factory* self) {
  if (self == nullptr) return;

  if (self->vtable != nullptr && self->vtable->destroy != nullptr) {
    self->vtable->destroy(self);
  }
  /* Note, we don't free(self) here because this object is always directly
   * embedded in another object. If tsi_ssl_handshaker_factory_init allocates
   * any memory, it should be free'd here. */
}

static tsi_ssl_handshaker_factory* tsi_ssl_handshaker_factory_ref(
    tsi_ssl_handshaker_factory* self) {
  if (self == nullptr) return nullptr;
  gpr_refn(&self->refcount, 1);
  return self;
}

static void tsi_ssl_handshaker_factory_unref(tsi_ssl_handshaker_factory* self) {
  if (self == nullptr) return;
  if (gpr_unref(&self->refcount)) {
    tsi_ssl_handshaker_factory_destroy(self);
  }
}

static tsi_ssl_handshaker_factory_vtable handshaker_factory_vtable = {nullptr};

/* Initializes a tsi_ssl_handshaker_factory object. Caller is responsible for
 * allocating memory for the factory. */
static void tsi_ssl_handshaker_factory_init(
    tsi_ssl_handshaker_factory* factory) {
  GPR_ASSERT(factory != nullptr);

  factory->vtable = &handshaker_factory_vtable;
  gpr_ref_init(&factory->refcount, 1);
}

/* --- tsi_handshaker_result methods implementation. ---*/
static tsi_result ssl_handshaker_result_extract_peer(
    const tsi_handshaker_result* self, tsi_peer* peer) {
  tsi_result result = TSI_OK;
  const unsigned char* alpn_selected = nullptr;
  unsigned int alpn_selected_len;
  const tsi_ssl_handshaker_result* impl =
      reinterpret_cast<const tsi_ssl_handshaker_result*>(self);
  X509* peer_cert = SSL_get_peer_certificate(impl->ssl);
  if (peer_cert != nullptr) {
    result = peer_from_x509(peer_cert, 1, peer);
    X509_free(peer_cert);
    if (result != TSI_OK) return result;
  }
    /*int cert_len = 0;
    const char* pem_cert = nullptr;
    if (sk_X509_num(SSL_get_peer_cert_chain(impl->ssl)) <= 0) {
      pem_cert = get_cert_ASN1(SSL_get_peer_certificate(impl->ssl), &cert_len);
    } else {
      pem_cert =
          impl->is_client
              ? get_cert_chain_ASN1_client(SSL_get_peer_cert_chain(impl->ssl))
              : get_cert_chain_ASN1_server(SSL_get_peer_certificate(impl->ssl),
                                           SSL_get_peer_cert_chain(impl->ssl));
    }
    if (pem_cert != nullptr) {
      result = extract_x509_subject_names_from_pem_cert(pem_cert, peer, 1);
      gpr_free((void*)pem_cert);
      if (result != TSI_OK) return result;
    }
    */
#if TSI_OPENSSL_ALPN_SUPPORT
  SSL_get0_alpn_selected(impl->ssl, &alpn_selected, &alpn_selected_len);
#endif /* TSI_OPENSSL_ALPN_SUPPORT */
  if (alpn_selected == nullptr) {
    /* Try npn. */
    SSL_get0_next_proto_negotiated(impl->ssl, &alpn_selected,
                                   &alpn_selected_len);
  }
  // 1 is for session reused property.
  size_t new_property_count = peer->property_count + 1;
  if (alpn_selected != nullptr) new_property_count++;
  tsi_peer_property* new_properties = static_cast<tsi_peer_property*>(
      gpr_zalloc(sizeof(*new_properties) * new_property_count));
  for (size_t i = 0; i < peer->property_count; i++) {
    new_properties[i] = peer->properties[i];
  }
  if (peer->properties != nullptr) gpr_free(peer->properties);
  peer->properties = new_properties;

  if (alpn_selected != nullptr) {
    result = tsi_construct_string_peer_property(
        TSI_SSL_ALPN_SELECTED_PROTOCOL,
        reinterpret_cast<const char*>(alpn_selected), alpn_selected_len,
        &peer->properties[peer->property_count]);
    if (result != TSI_OK) return result;
    peer->property_count++;
  }

  const char* session_reused = SSL_session_reused(impl->ssl) ? "true" : "false";
  result = tsi_construct_string_peer_property_from_cstring(
      TSI_SSL_SESSION_REUSED_PEER_PROPERTY, session_reused,
      &peer->properties[peer->property_count]);
  if (result != TSI_OK) return result;
  peer->property_count++;
  return result;
}

static tsi_result ssl_handshaker_result_create_frame_protector(
    const tsi_handshaker_result* self, size_t* max_output_protected_frame_size,
    tsi_frame_protector** protector) {
  size_t actual_max_output_protected_frame_size =
      TSI_SSL_MAX_PROTECTED_FRAME_SIZE_UPPER_BOUND;
  tsi_ssl_handshaker_result* impl =
      reinterpret_cast<tsi_ssl_handshaker_result*>(
          const_cast<tsi_handshaker_result*>(self));
  tsi_ssl_frame_protector* protector_impl =
      static_cast<tsi_ssl_frame_protector*>(
          gpr_zalloc(sizeof(*protector_impl)));

  if (max_output_protected_frame_size != nullptr) {
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
  protector_impl->buffer =
      static_cast<unsigned char*>(gpr_malloc(protector_impl->buffer_size));
  if (protector_impl->buffer == nullptr) {
    gpr_log(GPR_ERROR,
            "Could not allocated buffer for tsi_ssl_frame_protector.");
    gpr_free(protector_impl);
    return TSI_INTERNAL_ERROR;
  }

  /* Transfer ownership of ssl and network_io to the frame protector. */
  protector_impl->ssl = impl->ssl;
  impl->ssl = nullptr;
  protector_impl->network_io = impl->network_io;
  impl->network_io = nullptr;
  protector_impl->base.vtable = &frame_protector_vtable;
  *protector = &protector_impl->base;
  return TSI_OK;
}

static tsi_result ssl_handshaker_result_get_unused_bytes(
    const tsi_handshaker_result* self, const unsigned char** bytes,
    size_t* bytes_size) {
  const tsi_ssl_handshaker_result* impl =
      reinterpret_cast<const tsi_ssl_handshaker_result*>(self);
  *bytes_size = impl->unused_bytes_size;
  *bytes = impl->unused_bytes;
  return TSI_OK;
}

static void ssl_handshaker_result_destroy(tsi_handshaker_result* self) {
  tsi_ssl_handshaker_result* impl =
      reinterpret_cast<tsi_ssl_handshaker_result*>(self);
  SSL_free(impl->ssl);
  BIO_free(impl->network_io);
  gpr_free(impl->unused_bytes);
  gpr_free(impl);
}

static const tsi_handshaker_result_vtable handshaker_result_vtable = {
    ssl_handshaker_result_extract_peer,
    nullptr, /* create_zero_copy_grpc_protector */
    ssl_handshaker_result_create_frame_protector,
    ssl_handshaker_result_get_unused_bytes,
    ssl_handshaker_result_destroy,
};

static tsi_result ssl_handshaker_result_create(
    tsi_ssl_handshaker* handshaker, const unsigned char* unused_bytes,
    size_t unused_bytes_size, tsi_handshaker_result** handshaker_result) {
  if (handshaker == nullptr || handshaker_result == nullptr ||
      (unused_bytes_size > 0 && unused_bytes == nullptr)) {
    return TSI_INVALID_ARGUMENT;
  }
  tsi_ssl_handshaker_result* result =
      static_cast<tsi_ssl_handshaker_result*>(gpr_zalloc(sizeof(*result)));
  result->base.vtable = &handshaker_result_vtable;
  /* Transfer ownership of ssl and network_io to the handshaker result. */
  result->ssl = handshaker->ssl;
  handshaker->ssl = nullptr;
  result->network_io = handshaker->network_io;
  result->is_client = handshaker->is_client;
  handshaker->network_io = nullptr;
  if (unused_bytes_size > 0) {
    result->unused_bytes =
        static_cast<unsigned char*>(gpr_malloc(unused_bytes_size));
    memcpy(result->unused_bytes, unused_bytes, unused_bytes_size);
  }
  result->unused_bytes_size = unused_bytes_size;
  *handshaker_result = &result->base;
  return TSI_OK;
}

/* --- tsi_handshaker methods implementation. ---*/

static tsi_result ssl_handshaker_get_bytes_to_send_to_peer(
    tsi_ssl_handshaker* impl, unsigned char* bytes, size_t* bytes_size) {
  int bytes_read_from_ssl = 0;
  if (bytes == nullptr || bytes_size == nullptr || *bytes_size == 0 ||
      *bytes_size > INT_MAX) {
    return TSI_INVALID_ARGUMENT;
  }
  GPR_ASSERT(*bytes_size <= INT_MAX);
  bytes_read_from_ssl =
      BIO_read(impl->network_io, bytes, static_cast<int>(*bytes_size));
  if (bytes_read_from_ssl < 0) {
    *bytes_size = 0;
    if (!BIO_should_retry(impl->network_io)) {
      impl->result = TSI_INTERNAL_ERROR;
      return impl->result;
    } else {
      return TSI_OK;
    }
  }
  *bytes_size = static_cast<size_t>(bytes_read_from_ssl);
  return BIO_pending(impl->network_io) == 0 ? TSI_OK : TSI_INCOMPLETE_DATA;
}

static tsi_result ssl_handshaker_get_result(tsi_ssl_handshaker* impl) {
  if ((impl->result == TSI_HANDSHAKE_IN_PROGRESS) &&
      SSL_is_init_finished(impl->ssl)) {
    impl->result = TSI_OK;
  }
  return impl->result;
}

static tsi_result ssl_handshaker_process_bytes_from_peer(
    tsi_ssl_handshaker* impl, const unsigned char* bytes, size_t* bytes_size) {
  int bytes_written_into_ssl_size = 0;
  if (bytes == nullptr || bytes_size == nullptr || *bytes_size > INT_MAX) {
    return TSI_INVALID_ARGUMENT;
  }
  GPR_ASSERT(*bytes_size <= INT_MAX);
  bytes_written_into_ssl_size =
      BIO_write(impl->network_io, bytes, static_cast<int>(*bytes_size));
  if (bytes_written_into_ssl_size < 0) {
    gpr_log(GPR_ERROR, "Could not write to memory BIO.");
    impl->result = TSI_INTERNAL_ERROR;
    return impl->result;
  }
  *bytes_size = static_cast<size_t>(bytes_written_into_ssl_size);

  if (ssl_handshaker_get_result(impl) != TSI_HANDSHAKE_IN_PROGRESS) {
    impl->result = TSI_OK;
    return impl->result;
  } else {
    /* Get ready to get some bytes from SSL. */
    int ssl_result = SSL_do_handshake(impl->ssl);
    ssl_result = SSL_get_error(impl->ssl, ssl_result);
    switch (ssl_result) {
      case SSL_ERROR_WANT_READ:
        if (BIO_pending(impl->network_io) == 0) {
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

static void ssl_handshaker_destroy(tsi_handshaker* self) {
  tsi_ssl_handshaker* impl = reinterpret_cast<tsi_ssl_handshaker*>(self);
  SSL_free(impl->ssl);
  BIO_free(impl->network_io);
  gpr_free(impl->outgoing_bytes_buffer);
  gpr_free((void*)impl->server_name_indication);
  tsi_ssl_handshaker_factory_unref(impl->factory_ref);
  if (impl->alpn_protocols != nullptr) {
    gpr_free(impl->alpn_protocols);
  }
  gpr_free(impl);
}

static void tls_handshaker_destroy(tsi_handshaker* self) {
  tsi_ssl_handshaker* impl = reinterpret_cast<tsi_ssl_handshaker*>(self);
  if (impl->key_cert_pairs != nullptr) {
    for (size_t i = 0; i < impl->num_key_cert_pairs; i++) {
      gpr_free((void*)impl->key_cert_pairs[i].private_key);
      gpr_free((void*)impl->key_cert_pairs[i].cert_chain);
    }
    gpr_free(impl->key_cert_pairs);
  }
  if (impl->reload_arg != nullptr) {
    grpc_tls_credential_reload_arg* arg = impl->reload_arg;
    if (impl->reload_arg->key_materials_config != nullptr) {
      impl->reload_arg->key_materials_config->Unref();
    }
    gpr_free((void*)arg->error_details);
    gpr_free(arg);
  }
  tsi_ssl_handshaker_factory_unref(impl->factory_ref);
  ssl_handshaker_destroy(self);
}

static tsi_result ssl_handshaker_next(
    tsi_handshaker* self, const unsigned char* received_bytes,
    size_t received_bytes_size, const unsigned char** bytes_to_send,
    size_t* bytes_to_send_size, tsi_handshaker_result** handshaker_result,
    tsi_handshaker_on_next_done_cb cb, void* user_data) {
  /* Input sanity check.  */
  if ((received_bytes_size > 0 && received_bytes == nullptr) ||
      bytes_to_send == nullptr || bytes_to_send_size == nullptr ||
      handshaker_result == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }
  /* If there are received bytes, process them first.  */
  tsi_ssl_handshaker* impl = reinterpret_cast<tsi_ssl_handshaker*>(self);
  tsi_result status = TSI_OK;
  size_t bytes_consumed = received_bytes_size;
  if (received_bytes_size > 0) {
    status = ssl_handshaker_process_bytes_from_peer(impl, received_bytes,
                                                    &bytes_consumed);
    if (status != TSI_OK) return status;
  }
  /* Get bytes to send to the peer, if available.  */
  size_t offset = 0;
  do {
    size_t to_send_size = impl->outgoing_bytes_buffer_size - offset;
    status = ssl_handshaker_get_bytes_to_send_to_peer(
        impl, impl->outgoing_bytes_buffer + offset, &to_send_size);
    offset += to_send_size;
    if (status == TSI_INCOMPLETE_DATA) {
      impl->outgoing_bytes_buffer_size *= 2;
      impl->outgoing_bytes_buffer = static_cast<unsigned char*>(gpr_realloc(
          impl->outgoing_bytes_buffer, impl->outgoing_bytes_buffer_size));
    }
  } while (status == TSI_INCOMPLETE_DATA);
  if (status != TSI_OK) return status;
  *bytes_to_send = impl->outgoing_bytes_buffer;
  *bytes_to_send_size = offset;
  /* If handshake completes, create tsi_handshaker_result.  */
  if (ssl_handshaker_get_result(impl) == TSI_HANDSHAKE_IN_PROGRESS) {
    *handshaker_result = nullptr;
  } else {
    size_t unused_bytes_size = received_bytes_size - bytes_consumed;
    const unsigned char* unused_bytes =
        unused_bytes_size == 0 ? nullptr : received_bytes + bytes_consumed;
    status = ssl_handshaker_result_create(impl, unused_bytes, unused_bytes_size,
                                          handshaker_result);
    if (status == TSI_OK) {
      /* Indicates that the handshake has completed and that a handshaker_result
       * has been created. */
      self->handshaker_result_created = true;
    }
  }
  return status;
}

static void on_credential_reloaded(void* arg, grpc_error* error) {
  tsi_ssl_handshaker* handshaker = static_cast<tsi_ssl_handshaker*>(arg);
  GPR_ASSERT(handshaker != nullptr);
  tsi_handshaker_on_next_done_cb cb = handshaker->handshaker_next_arg.cb;
  void* user_data = handshaker->handshaker_next_arg.user_data;
  tsi_result result = update_ssl_handshaker(handshaker, error);
  if (result != TSI_OK) {
    cb(TSI_INTERNAL_ERROR, user_data, nullptr, 0, nullptr);
    return;
  }
  const unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  tsi_handshaker_result* handshaker_result = nullptr;
  result = ssl_handshaker_next(
      &handshaker->base, handshaker->handshaker_next_arg.received_bytes,
      handshaker->handshaker_next_arg.received_bytes_size, &bytes_to_send,
      &bytes_to_send_size, &handshaker_result, cb, user_data);
  cb(result, user_data, bytes_to_send, bytes_to_send_size, handshaker_result);
  return;
}

static tsi_result tls_handshaker_next(
    tsi_handshaker* self, const unsigned char* received_bytes,
    size_t received_bytes_size, const unsigned char** bytes_to_send,
    size_t* bytes_to_send_size, tsi_handshaker_result** handshaker_result,
    tsi_handshaker_on_next_done_cb cb, void* user_data) {
  tsi_ssl_handshaker* handshaker = reinterpret_cast<tsi_ssl_handshaker*>(self);
  if (!handshaker->is_client && !handshaker->credential_reloaded &&
      (received_bytes == nullptr || received_bytes_size == 0)) {
    *handshaker_result = nullptr;
    return TSI_OK;
  }
  if (!handshaker->credential_reloaded) {
    handshaker->credential_reloaded = true;
    /* Populate handshaker_next_arg field. */
    handshaker->handshaker_next_arg.received_bytes = received_bytes;
    handshaker->handshaker_next_arg.received_bytes_size = received_bytes_size;
    handshaker->handshaker_next_arg.cb = cb;
    handshaker->handshaker_next_arg.user_data = user_data;
    /* Populate reload_arg field. */
    GRPC_CLOSURE_INIT(&handshaker->on_credential_reloaded,
                      on_credential_reloaded, self, grpc_schedule_on_exec_ctx);
    handshaker->reload_arg->cb_user_data = handshaker;
    handshaker->reload_arg->cb = credential_reload_done_cb;
    handshaker->reload_arg->status = GRPC_STATUS_OK;
    grpc_tls_credentials_options* options = handshaker->options;
    GPR_ASSERT(options != nullptr);
    const grpc_tls_credential_reload_config* config =
        options->credential_reload_config();
    if (config != nullptr) {
      int callback_status = config->schedule(handshaker->reload_arg);
      /* Handle asynchronously. */
      if (callback_status) {
        return TSI_ASYNC;
      }
    }
    /* Handle synchronously or credential reload config is not specified. */
    tsi_result result = update_ssl_handshaker(
        handshaker, process_credential_reload_result(handshaker->reload_arg));
    if (result != TSI_OK) {
      return TSI_FAILED_PRECONDITION;
    }
  }
  return ssl_handshaker_next(self, received_bytes, received_bytes_size,
                             bytes_to_send, bytes_to_send_size,
                             handshaker_result, cb, user_data);
}

static void tls_handshaker_shutdown(tsi_handshaker* self) {
  GPR_ASSERT(self != nullptr);
  if (self->handshake_shutdown) {
    return;
  }
  tsi_ssl_handshaker* handshaker = reinterpret_cast<tsi_ssl_handshaker*>(self);
  GPR_ASSERT(handshaker->options != nullptr);
  const grpc_tls_credential_reload_config* config =
      handshaker->options->credential_reload_config();
  if (config != nullptr) {
    config->cancel(handshaker->reload_arg);
  }
}

static const tsi_handshaker_vtable handshaker_vtable = {
    nullptr, /* get_bytes_to_send_to_peer -- deprecated */
    nullptr, /* process_bytes_from_peer   -- deprecated */
    nullptr, /* get_result                -- deprecated */
    nullptr, /* extract_peer              -- deprecated */
    nullptr, /* create_frame_protector    -- deprecated */
    ssl_handshaker_destroy,
    ssl_handshaker_next,
    nullptr, /* shutdown */
};

static const tsi_handshaker_vtable tls_handshaker_vtable = {
    nullptr, /* get_bytes_to_send_to_peer -- deprecated */
    nullptr, /* process_bytes_from_peer   -- deprecated */
    nullptr, /* get_result                -- deprecated */
    nullptr, /* extract_peer              -- deprecated */
    nullptr, /* create_frame_protector    -- deprecated */
    tls_handshaker_destroy,
    tls_handshaker_next,
    tls_handshaker_shutdown, /* shutdown */
};

/* --- tsi_ssl_handshaker_factory common methods. --- */

static void tsi_ssl_handshaker_resume_session(
    SSL* ssl, tsi::SslSessionLRUCache* session_cache) {
  const char* server_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (server_name == nullptr) {
    return;
  }
  tsi::SslSessionPtr session = session_cache->Get(server_name);
  if (session != nullptr) {
    // SSL_set_session internally increments reference counter.
    SSL_set_session(ssl, session.get());
  }
}

static tsi_result create_tsi_ssl_handshaker(SSL_CTX* ctx, int is_client,
                                            const char* server_name_indication,
                                            tsi_ssl_handshaker_factory* factory,
                                            tsi_handshaker** handshaker) {
  tsi_ssl_handshaker* ssl_handshaker =
      static_cast<tsi_ssl_handshaker*>(gpr_zalloc(sizeof(*ssl_handshaker)));
  tsi::SslSessionLRUCache* session_cache = nullptr;
  if (is_client) {
    tsi_ssl_client_handshaker_factory* client_factory =
        reinterpret_cast<tsi_ssl_client_handshaker_factory*>(factory);
    if (client_factory->session_cache != nullptr) {
      session_cache = client_factory->session_cache.get();
    }
  }
  ssl_handshaker->base.vtable = &handshaker_vtable;
  tsi_result result =
      populate_ssl_handshaker(ssl_handshaker, ctx, is_client,
                              server_name_indication, factory, session_cache);
  if (result != TSI_OK) {
    gpr_free(ssl_handshaker);
    return result;
  }
  *handshaker = &ssl_handshaker->base;
  return result;
}

static int select_protocol_list(const unsigned char** out,
                                unsigned char* outlen,
                                const unsigned char* client_list,
                                size_t client_list_len,
                                const unsigned char* server_list,
                                size_t server_list_len) {
  const unsigned char* client_current = client_list;
  while (static_cast<unsigned int>(client_current - client_list) <
         client_list_len) {
    unsigned char client_current_len = *(client_current++);
    const unsigned char* server_current = server_list;
    while ((server_current >= server_list) &&
           static_cast<uintptr_t>(server_current - server_list) <
               server_list_len) {
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

/* --- tsi_ssl_client_handshaker_factory methods implementation. --- */

tsi_result tsi_ssl_client_handshaker_factory_create_handshaker(
    tsi_ssl_client_handshaker_factory* self, const char* server_name_indication,
    tsi_handshaker** handshaker) {
  return create_tsi_ssl_handshaker(self->ssl_context, 1, server_name_indication,
                                   &self->base, handshaker);
}

void tsi_ssl_client_handshaker_factory_unref(
    tsi_ssl_client_handshaker_factory* self) {
  if (self == nullptr) return;
  tsi_ssl_handshaker_factory_unref(&self->base);
}

static void tsi_ssl_client_handshaker_factory_destroy(
    tsi_ssl_handshaker_factory* factory) {
  if (factory == nullptr) return;
  tsi_ssl_client_handshaker_factory* self =
      reinterpret_cast<tsi_ssl_client_handshaker_factory*>(factory);
  if (self->ssl_context != nullptr) SSL_CTX_free(self->ssl_context);
  if (self->alpn_protocol_list != nullptr) gpr_free(self->alpn_protocol_list);
  self->session_cache.reset();
  gpr_free(self);
}

static int client_handshaker_factory_npn_callback(SSL* ssl, unsigned char** out,
                                                  unsigned char* outlen,
                                                  const unsigned char* in,
                                                  unsigned int inlen,
                                                  void* arg) {
  tsi_ssl_client_handshaker_factory* factory =
      static_cast<tsi_ssl_client_handshaker_factory*>(arg);
  return select_protocol_list((const unsigned char**)out, outlen,
                              factory->alpn_protocol_list,
                              factory->alpn_protocol_list_length, in, inlen);
}

/* --- tsi_ssl_server_handshaker_factory methods implementation. --- */

tsi_result tsi_ssl_server_handshaker_factory_create_handshaker(
    tsi_ssl_server_handshaker_factory* self, tsi_handshaker** handshaker) {
  if (self->ssl_context_count == 0) return TSI_INVALID_ARGUMENT;
  /* Create the handshaker with the first context. We will switch if needed
     because of SNI in ssl_server_handshaker_factory_servername_callback.  */
  return create_tsi_ssl_handshaker(self->ssl_contexts[0], 0, nullptr,
                                   &self->base, handshaker);
}

void tsi_ssl_server_handshaker_factory_unref(
    tsi_ssl_server_handshaker_factory* self) {
  if (self == nullptr) return;
  tsi_ssl_handshaker_factory_unref(&self->base);
}

static void tsi_ssl_server_handshaker_factory_destroy(
    tsi_ssl_handshaker_factory* factory) {
  if (factory == nullptr) return;
  tsi_ssl_server_handshaker_factory* self =
      reinterpret_cast<tsi_ssl_server_handshaker_factory*>(factory);
  size_t i;
  for (i = 0; i < self->ssl_context_count; i++) {
    if (self->ssl_contexts[i] != nullptr) {
      SSL_CTX_free(self->ssl_contexts[i]);
      tsi_peer_destruct(&self->ssl_context_x509_subject_names[i]);
    }
  }
  if (self->ssl_contexts != nullptr) gpr_free(self->ssl_contexts);
  if (self->ssl_context_x509_subject_names != nullptr) {
    gpr_free(self->ssl_context_x509_subject_names);
  }
  if (self->alpn_protocol_list != nullptr) gpr_free(self->alpn_protocol_list);
  gpr_free(self);
}

static int does_entry_match_name(const char* entry, size_t entry_length,
                                 const char* name) {
  const char* dot;
  const char* name_subdomain = nullptr;
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
  if (name_subdomain == nullptr) return 0;
  name_subdomain_length = strlen(name_subdomain);
  if (name_subdomain_length < 2) return 0;
  name_subdomain++; /* Starts after the dot. */
  name_subdomain_length--;
  entry += 2; /* Remove *. */
  entry_length -= 2;
  dot = strchr(name_subdomain, '.');
  if ((dot == nullptr) || (dot == &name_subdomain[name_subdomain_length - 1])) {
    gpr_log(GPR_ERROR, "Invalid toplevel subdomain: %s", name_subdomain);
    return 0;
  }
  if (name_subdomain[name_subdomain_length - 1] == '.') {
    name_subdomain_length--;
  }
  return ((entry_length > 0) && (name_subdomain_length == entry_length) &&
          strncmp(entry, name_subdomain, entry_length) == 0);
}

static int ssl_server_handshaker_factory_servername_callback(SSL* ssl, int* ap,
                                                             void* arg) {
  tsi_ssl_server_handshaker_factory* impl =
      static_cast<tsi_ssl_server_handshaker_factory*>(arg);
  size_t i = 0;
  const char* servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (servername == nullptr || strlen(servername) == 0) {
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
    SSL* ssl, const unsigned char** out, unsigned char* outlen,
    const unsigned char* in, unsigned int inlen, void* arg) {
  tsi_ssl_server_handshaker_factory* factory =
      static_cast<tsi_ssl_server_handshaker_factory*>(arg);
  return select_protocol_list(out, outlen, in, inlen,
                              factory->alpn_protocol_list,
                              factory->alpn_protocol_list_length);
}
#endif /* TSI_OPENSSL_ALPN_SUPPORT */

static int server_handshaker_factory_npn_advertised_callback(
    SSL* ssl, const unsigned char** out, unsigned int* outlen, void* arg) {
  tsi_ssl_server_handshaker_factory* factory =
      static_cast<tsi_ssl_server_handshaker_factory*>(arg);
  *out = factory->alpn_protocol_list;
  GPR_ASSERT(factory->alpn_protocol_list_length <= UINT_MAX);
  *outlen = static_cast<unsigned int>(factory->alpn_protocol_list_length);
  return SSL_TLSEXT_ERR_OK;
}

/// This callback is called when new \a session is established and ready to
/// be cached. This session can be reused for new connections to similar
/// servers at later point of time.
/// It's intended to be used with SSL_CTX_sess_set_new_cb function.
///
/// It returns 1 if callback takes ownership over \a session and 0 otherwise.
static int server_handshaker_factory_new_session_callback(
    SSL* ssl, SSL_SESSION* session) {
  SSL_CTX* ssl_context = SSL_get_SSL_CTX(ssl);
  if (ssl_context == nullptr) {
    return 0;
  }
  void* arg = SSL_CTX_get_ex_data(ssl_context, g_ssl_ctx_ex_factory_index);
  tsi_ssl_client_handshaker_factory* factory =
      static_cast<tsi_ssl_client_handshaker_factory*>(arg);
  const char* server_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (server_name == nullptr) {
    return 0;
  }
  factory->session_cache->Put(server_name, tsi::SslSessionPtr(session));
  // Return 1 to indicate transfered ownership over the given session.
  return 1;
}

/* --- tsi_ssl_handshaker_factory constructors. --- */

static tsi_ssl_handshaker_factory_vtable client_handshaker_factory_vtable = {
    tsi_ssl_client_handshaker_factory_destroy};

tsi_result tsi_create_ssl_client_handshaker_factory(
    const tsi_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const char* pem_root_certs, const char* cipher_suites,
    const char** alpn_protocols, uint16_t num_alpn_protocols,
    tsi_ssl_client_handshaker_factory** factory) {
  tsi_ssl_client_handshaker_options options;
  memset(&options, 0, sizeof(options));
  options.pem_key_cert_pair = pem_key_cert_pair;
  options.pem_root_certs = pem_root_certs;
  options.cipher_suites = cipher_suites;
  options.alpn_protocols = alpn_protocols;
  options.num_alpn_protocols = num_alpn_protocols;
  return tsi_create_ssl_client_handshaker_factory_with_options(&options,
                                                               factory);
}

tsi_result tsi_create_ssl_client_handshaker_factory_with_options(
    const tsi_ssl_client_handshaker_options* options,
    tsi_ssl_client_handshaker_factory** factory) {
  SSL_CTX* ssl_context = nullptr;
  tsi_ssl_client_handshaker_factory* impl = nullptr;
  tsi_result result = TSI_OK;

  gpr_once_init(&g_init_openssl_once, init_openssl);

  if (factory == nullptr) return TSI_INVALID_ARGUMENT;
  *factory = nullptr;
  if (options->pem_root_certs == nullptr && options->root_store == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }

#if defined(OPENSSL_NO_TLS1_2_METHOD) || OPENSSL_API_COMPAT >= 0x10100000L
  ssl_context = SSL_CTX_new(TLS_method());
#else
  ssl_context = SSL_CTX_new(TLSv1_2_method());
#endif
  if (ssl_context == nullptr) {
    gpr_log(GPR_ERROR, "Could not create ssl context.");
    return TSI_INVALID_ARGUMENT;
  }

  impl = static_cast<tsi_ssl_client_handshaker_factory*>(
      gpr_zalloc(sizeof(*impl)));
  tsi_ssl_handshaker_factory_init(&impl->base);
  impl->base.vtable = &client_handshaker_factory_vtable;
  impl->ssl_context = ssl_context;
  if (options->session_cache != nullptr) {
    // Unref is called manually on factory destruction.
    impl->session_cache =
        reinterpret_cast<tsi::SslSessionLRUCache*>(options->session_cache)
            ->Ref();
    SSL_CTX_set_ex_data(ssl_context, g_ssl_ctx_ex_factory_index, impl);
    SSL_CTX_sess_set_new_cb(ssl_context,
                            server_handshaker_factory_new_session_callback);
    SSL_CTX_set_session_cache_mode(ssl_context, SSL_SESS_CACHE_CLIENT);
  }

  do {
    result = populate_ssl_context(ssl_context, options->pem_key_cert_pair,
                                  options->cipher_suites);
    if (result != TSI_OK) break;

#if OPENSSL_VERSION_NUMBER >= 0x10100000
    // X509_STORE_up_ref is only available since OpenSSL 1.1.
    if (options->root_store != nullptr) {
      X509_STORE_up_ref(options->root_store->store);
      SSL_CTX_set_cert_store(ssl_context, options->root_store->store);
    }
#endif
    if (OPENSSL_VERSION_NUMBER < 0x10100000 || options->root_store == nullptr) {
      result = ssl_ctx_load_verification_certs(
          ssl_context, options->pem_root_certs, strlen(options->pem_root_certs),
          nullptr);
      if (result != TSI_OK) {
        gpr_log(GPR_ERROR, "Cannot load server root certificates.");
        break;
      }
    }

    if (options->num_alpn_protocols != 0) {
      result = build_alpn_protocol_name_list(
          options->alpn_protocols, options->num_alpn_protocols,
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
              static_cast<unsigned int>(impl->alpn_protocol_list_length))) {
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
    tsi_ssl_handshaker_factory_unref(&impl->base);
    return result;
  }
  SSL_CTX_set_verify(ssl_context, SSL_VERIFY_PEER, nullptr);
  /* TODO(jboeuf): Add revocation verification. */

  *factory = impl;
  return TSI_OK;
}

static tsi_ssl_handshaker_factory_vtable server_handshaker_factory_vtable = {
    tsi_ssl_server_handshaker_factory_destroy};

tsi_result tsi_create_ssl_server_handshaker_factory(
    const tsi_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs, const char* pem_client_root_certs,
    int force_client_auth, const char* cipher_suites,
    const char** alpn_protocols, uint16_t num_alpn_protocols,
    tsi_ssl_server_handshaker_factory** factory) {
  return tsi_create_ssl_server_handshaker_factory_ex(
      pem_key_cert_pairs, num_key_cert_pairs, pem_client_root_certs,
      force_client_auth ? TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
                        : TSI_DONT_REQUEST_CLIENT_CERTIFICATE,
      cipher_suites, alpn_protocols, num_alpn_protocols, factory);
}

tsi_result tsi_create_ssl_server_handshaker_factory_ex(
    const tsi_ssl_pem_key_cert_pair* pem_key_cert_pairs,
    size_t num_key_cert_pairs, const char* pem_client_root_certs,
    tsi_client_certificate_request_type client_certificate_request,
    const char* cipher_suites, const char** alpn_protocols,
    uint16_t num_alpn_protocols, tsi_ssl_server_handshaker_factory** factory) {
  tsi_ssl_server_handshaker_options options;
  memset(&options, 0, sizeof(options));
  options.pem_key_cert_pairs = pem_key_cert_pairs;
  options.num_key_cert_pairs = num_key_cert_pairs;
  options.pem_client_root_certs = pem_client_root_certs;
  options.client_certificate_request = client_certificate_request;
  options.cipher_suites = cipher_suites;
  options.alpn_protocols = alpn_protocols;
  options.num_alpn_protocols = num_alpn_protocols;
  return tsi_create_ssl_server_handshaker_factory_with_options(&options,
                                                               factory);
}

tsi_result tsi_create_ssl_server_handshaker_factory_with_options(
    const tsi_ssl_server_handshaker_options* options,
    tsi_ssl_server_handshaker_factory** factory) {
  tsi_ssl_server_handshaker_factory* impl = nullptr;
  tsi_result result = TSI_OK;
  size_t i = 0;

  gpr_once_init(&g_init_openssl_once, init_openssl);

  if (factory == nullptr) return TSI_INVALID_ARGUMENT;
  *factory = nullptr;
  if (options->num_key_cert_pairs == 0 ||
      options->pem_key_cert_pairs == nullptr) {
    return TSI_INVALID_ARGUMENT;
  }

  impl = static_cast<tsi_ssl_server_handshaker_factory*>(
      gpr_zalloc(sizeof(*impl)));
  tsi_ssl_handshaker_factory_init(&impl->base);
  impl->base.vtable = &server_handshaker_factory_vtable;

  impl->ssl_contexts = static_cast<SSL_CTX**>(
      gpr_zalloc(options->num_key_cert_pairs * sizeof(SSL_CTX*)));
  impl->ssl_context_x509_subject_names = static_cast<tsi_peer*>(
      gpr_zalloc(options->num_key_cert_pairs * sizeof(tsi_peer)));
  if (impl->ssl_contexts == nullptr ||
      impl->ssl_context_x509_subject_names == nullptr) {
    tsi_ssl_handshaker_factory_unref(&impl->base);
    return TSI_OUT_OF_RESOURCES;
  }
  impl->ssl_context_count = options->num_key_cert_pairs;

  if (options->num_alpn_protocols > 0) {
    result = build_alpn_protocol_name_list(
        options->alpn_protocols, options->num_alpn_protocols,
        &impl->alpn_protocol_list, &impl->alpn_protocol_list_length);
    if (result != TSI_OK) {
      tsi_ssl_handshaker_factory_unref(&impl->base);
      return result;
    }
  }

  for (i = 0; i < options->num_key_cert_pairs; i++) {
    do {
#if defined(OPENSSL_NO_TLS1_2_METHOD) || OPENSSL_API_COMPAT >= 0x10100000L
      impl->ssl_contexts[i] = SSL_CTX_new(TLS_method());
#else
      impl->ssl_contexts[i] = SSL_CTX_new(TLSv1_2_method());
#endif
      if (impl->ssl_contexts[i] == nullptr) {
        gpr_log(GPR_ERROR, "Could not create ssl context.");
        result = TSI_OUT_OF_RESOURCES;
        break;
      }
      result = populate_ssl_context(impl->ssl_contexts[i],
                                    &options->pem_key_cert_pairs[i],
                                    options->cipher_suites);
      if (result != TSI_OK) break;

      // TODO(elessar): Provide ability to disable session ticket keys.

      // Allow client cache sessions (it's needed for OpenSSL only).
      int set_sid_ctx_result = SSL_CTX_set_session_id_context(
          impl->ssl_contexts[i], kSslSessionIdContext,
          GPR_ARRAY_SIZE(kSslSessionIdContext));
      if (set_sid_ctx_result == 0) {
        gpr_log(GPR_ERROR, "Failed to set session id context.");
        result = TSI_INTERNAL_ERROR;
        break;
      }

      if (options->session_ticket_key != nullptr) {
        if (SSL_CTX_set_tlsext_ticket_keys(
                impl->ssl_contexts[i],
                const_cast<char*>(options->session_ticket_key),
                options->session_ticket_key_size) == 0) {
          gpr_log(GPR_ERROR, "Invalid STEK size.");
          result = TSI_INVALID_ARGUMENT;
          break;
        }
      }

      if (options->pem_client_root_certs != nullptr) {
        STACK_OF(X509_NAME)* root_names = nullptr;
        result = ssl_ctx_load_verification_certs(
            impl->ssl_contexts[i], options->pem_client_root_certs,
            strlen(options->pem_client_root_certs), &root_names);
        if (result != TSI_OK) {
          gpr_log(GPR_ERROR, "Invalid verification certs.");
          break;
        }
        SSL_CTX_set_client_CA_list(impl->ssl_contexts[i], root_names);
      }
      switch (options->client_certificate_request) {
        case TSI_DONT_REQUEST_CLIENT_CERTIFICATE:
          SSL_CTX_set_verify(impl->ssl_contexts[i], SSL_VERIFY_NONE, nullptr);
          break;
        case TSI_REQUEST_CLIENT_CERTIFICATE_BUT_DONT_VERIFY:
          SSL_CTX_set_verify(impl->ssl_contexts[i], SSL_VERIFY_PEER,
                             NullVerifyCallback);
          break;
        case TSI_REQUEST_CLIENT_CERTIFICATE_AND_VERIFY:
          SSL_CTX_set_verify(impl->ssl_contexts[i], SSL_VERIFY_PEER, nullptr);
          break;
        case TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY:
          SSL_CTX_set_verify(impl->ssl_contexts[i],
                             SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                             NullVerifyCallback);
          break;
        case TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY:
          SSL_CTX_set_verify(impl->ssl_contexts[i],
                             SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                             nullptr);
          break;
      }
      /* TODO(jboeuf): Add revocation verification. */

      result = extract_x509_subject_names_from_pem_cert(
          options->pem_key_cert_pairs[i].cert_chain,
          &impl->ssl_context_x509_subject_names[i],
          0 /* include_certificate_type */);
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
      tsi_ssl_handshaker_factory_unref(&impl->base);
      return result;
    }
  }

  *factory = impl;
  return TSI_OK;
}

/* --- tsi_ssl utils. --- */

int tsi_ssl_peer_matches_name(const tsi_peer* peer, const char* name) {
  size_t i = 0;
  size_t san_count = 0;
  const tsi_peer_property* cn_property = nullptr;
  int like_ip = looks_like_ip_address(name);

  /* Check the SAN first. */
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* property = &peer->properties[i];
    if (property->name == nullptr) continue;
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
  if (san_count == 0 && cn_property != nullptr && !like_ip) {
    if (does_entry_match_name(cn_property->value.data,
                              cn_property->value.length, name)) {
      return 1;
    }
  }

  return 0; /* Not found. */
}

/* --- Testing support. --- */
const tsi_ssl_handshaker_factory_vtable* tsi_ssl_handshaker_factory_swap_vtable(
    tsi_ssl_handshaker_factory* factory,
    tsi_ssl_handshaker_factory_vtable* new_vtable) {
  GPR_ASSERT(factory != nullptr);
  GPR_ASSERT(factory->vtable != nullptr);

  const tsi_ssl_handshaker_factory_vtable* orig_vtable = factory->vtable;
  factory->vtable = new_vtable;
  return orig_vtable;
}

/* --- TLS TSI implementations --- */
tsi_result tls_tsi_handshaker_create(
    const char* server_name_indication, tsi_ssl_session_cache* session_cache,
    const grpc_tls_credentials_options* options, bool is_client,
    tsi_handshaker** handshaker) {
  tsi_ssl_handshaker* impl =
      static_cast<tsi_ssl_handshaker*>(gpr_zalloc(sizeof(*impl)));
  impl->options = const_cast<grpc_tls_credentials_options*>(options);
  grpc_tls_credential_reload_arg* reload_arg =
      static_cast<grpc_tls_credential_reload_arg*>(
          gpr_zalloc(sizeof(*reload_arg)));
  reload_arg->status = GRPC_STATUS_OK;
  reload_arg->key_materials_config = grpc_tls_key_materials_config_create();
  const grpc_tls_key_materials_config* config = options->key_materials_config();
  if (config != nullptr) {
    grpc_tls_key_materials_config_set_key_materials(
        reload_arg->key_materials_config, config->pem_key_cert_pairs(),
        config->pem_root_certs(), config->num_key_cert_pairs());
  }
  impl->reload_arg = reload_arg;
  impl->server_name_indication = gpr_strdup(server_name_indication);
  impl->session_cache = session_cache;
  impl->is_client = is_client;
  impl->alpn_protocols =
      tsi_fill_alpn_protocol_strings(&impl->num_alpn_protocols);
  impl->pem_root_certs = tsi::DefaultSslRootStore::GetPemRootCerts();
  impl->root_store = tsi::DefaultSslRootStore::GetRootStore();
  impl->base.vtable = &tls_handshaker_vtable;
  *handshaker = &impl->base;
  return TSI_OK;
}
/*
static char* get_cert_ASN1(X509* cert, int* len) {
  if (cert == nullptr) {
    return nullptr;
  }
  BIO* bio = BIO_new(BIO_s_mem());
  if (!PEM_write_bio_X509(bio, cert)) {
    BIO_free(bio);
    return nullptr;
  }
  char* contents;
  *len = BIO_get_mem_data(bio, &contents);
  char* result = static_cast<char*>(gpr_zalloc(*len + 1));
  memcpy(result, contents, *len);
  BIO_free(bio);
  return result;
}

static char* get_cert_chain_ASN1_client(STACK_OF(X509) * cert_chain) {
  size_t num_certs = sk_X509_num(cert_chain);
  GPR_ASSERT(num_certs > 0);
  char** cert_array =
      static_cast<char**>(gpr_zalloc(sizeof(char*) * num_certs));
  int* len_array = static_cast<int*>(gpr_zalloc(sizeof(int) * num_certs));
  int total_len = 0;
  for (size_t ind = 0; ind < num_certs; ind++) {
    cert_array[ind] =
        get_cert_ASN1(sk_X509_value(cert_chain, ind), &len_array[ind]);
    total_len += len_array[ind];
  }
  unsigned char* result =
      static_cast<unsigned char*>(gpr_zalloc(total_len + 1));
  int prev_len = 0;
  for (size_t ind = 0; ind < num_certs; ind++) {
    memcpy(result + prev_len, cert_array[ind], len_array[ind]);
    prev_len += len_array[ind];
  }
  // Free memory
  for (size_t ind = 0; ind < num_certs; ind++) {
    gpr_free(cert_array[ind]);
  }
  gpr_free(cert_array);
  gpr_free(len_array);
  for (size_t ind = 0; ind < (size_t)sk_X509_num(cert_chain) - 1; ind++) {
    X509_free(sk_X509_value(cert_chain, ind));
  }
  return (char*)result;
}

static char* get_cert_chain_ASN1_server(X509* leaf_cert,
                                        STACK_OF(X509) * cert_chain) {
  GPR_ASSERT(leaf_cert != nullptr);
  GPR_ASSERT(sk_X509_num(cert_chain) > 0);
  size_t num_certs = sk_X509_num(cert_chain) + 1;
  char** cert_array =
      static_cast<char**>(gpr_zalloc(sizeof(char*) * num_certs));
  int* len_array = static_cast<int*>(gpr_zalloc(sizeof(int) * num_certs));
  cert_array[0] = get_cert_ASN1(leaf_cert, &len_array[0]);
  int total_len = len_array[0];
  for (size_t ind = 1; ind < num_certs; ind++) {
    cert_array[ind] =
        get_cert_ASN1(sk_X509_value(cert_chain, ind - 1), &len_array[ind]);
    total_len += len_array[ind];
  }
  char* result = static_cast<char*>(gpr_zalloc(total_len + 1));
  int prev_len = 0;
  for (size_t ind = 0; ind < num_certs; ind++) {
    memcpy(result + prev_len, cert_array[ind], len_array[ind]);
    prev_len += len_array[ind];
  }
  // Free memory.
  for (size_t ind = 0; ind < num_certs; ind++) {
    gpr_free(cert_array[ind]);
  }
  gpr_free(cert_array);
  gpr_free(len_array);
  X509_free(leaf_cert);
  for (size_t ind = 0; ind < (size_t)sk_X509_num(cert_chain) - 1; ind++) {
    X509_free(sk_X509_value(cert_chain, ind));
  }
  return result;
}
*/

static tsi_result populate_ssl_handshaker(
    tsi_ssl_handshaker* handshaker, SSL_CTX* ctx, int is_client,
    const char* server_name_indication, tsi_ssl_handshaker_factory* factory,
    tsi::SslSessionLRUCache* session_cache) {
  GPR_ASSERT(handshaker != nullptr);
  SSL* ssl = SSL_new(ctx);
  BIO* network_io = nullptr;
  BIO* ssl_io = nullptr;
  if (ctx == nullptr) {
    gpr_log(GPR_ERROR, "SSL Context is null. Should never happen.");
    return TSI_INTERNAL_ERROR;
  }
  if (ssl == nullptr) {
    return TSI_OUT_OF_RESOURCES;
  }
  SSL_set_info_callback(ssl, ssl_info_callback);
  if (!BIO_new_bio_pair(&network_io, 0, &ssl_io, 0)) {
    gpr_log(GPR_ERROR, "BIO_new_bio_pair failed.");
    SSL_free(ssl);
    return TSI_OUT_OF_RESOURCES;
  }
  SSL_set_bio(ssl, ssl_io, ssl_io);
  if (is_client) {
    int ssl_result;
    SSL_set_connect_state(ssl);
    if (server_name_indication != nullptr) {
      if (!SSL_set_tlsext_host_name(ssl, server_name_indication)) {
        gpr_log(GPR_ERROR, "Invalid server name indication %s.",
                server_name_indication);
        SSL_free(ssl);
        BIO_free(network_io);
        return TSI_INTERNAL_ERROR;
      }
    }
    if (session_cache != nullptr) {
      tsi_ssl_handshaker_resume_session(ssl, session_cache);
    }
    ssl_result = SSL_do_handshake(ssl);
    ssl_result = SSL_get_error(ssl, ssl_result);
    if (ssl_result != SSL_ERROR_WANT_READ) {
      gpr_log(GPR_ERROR,
              "Unexpected error received from first SSL_do_handshake call: %s",
              ssl_error_string(ssl_result));
      SSL_free(ssl);
      BIO_free(network_io);
      return TSI_INTERNAL_ERROR;
    }
  } else {
    SSL_set_accept_state(ssl);
  }
  handshaker->ssl = ssl;
  handshaker->network_io = network_io;
  handshaker->result = TSI_HANDSHAKE_IN_PROGRESS;
  handshaker->outgoing_bytes_buffer_size =
      TSI_SSL_HANDSHAKER_OUTGOING_BUFFER_INITIAL_SIZE;
  handshaker->outgoing_bytes_buffer = static_cast<unsigned char*>(
      gpr_zalloc(handshaker->outgoing_bytes_buffer_size));
  handshaker->factory_ref = tsi_ssl_handshaker_factory_ref(factory);
  handshaker->is_client = is_client;
  return TSI_OK;
}

static void credential_reload_done_cb(grpc_tls_credential_reload_arg* arg) {
  GPR_ASSERT(arg != nullptr);
  grpc_core::ExecCtx exec_ctx;
  tsi_ssl_handshaker* handshaker =
      static_cast<tsi_ssl_handshaker*>(arg->cb_user_data);
  grpc_error* error = process_credential_reload_result(arg);
  GRPC_CLOSURE_SCHED(&handshaker->on_credential_reloaded, error);
}

static grpc_error* process_credential_reload_result(
    grpc_tls_credential_reload_arg* arg) {
  grpc_error* error = GRPC_ERROR_NONE;
  char* msg = nullptr;
  /* Credential reload is cancelled by the caller. */
  if (arg->status == GRPC_STATUS_CANCELLED) {
    gpr_asprintf(&msg,
                 "credential reload is cancelled by the caller with error: %s",
                 arg->error_details);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    /* Credential reload did not finish correctly. */
  } else if (arg->status != GRPC_STATUS_OK) {
    gpr_asprintf(&msg,
                 "Credential reload did not finish correctly with error: %s",
                 arg->error_details);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
  }
  gpr_free(msg);
  return error;
}

static tsi_result update_ssl_handshaker(tsi_ssl_handshaker* handshaker,
                                        grpc_error* error) {
  if (error != GRPC_ERROR_NONE) {
    return TSI_INTERNAL_ERROR;
  }
  bool is_client = handshaker->is_client;
  grpc_tls_credential_reload_arg* reload_arg = handshaker->reload_arg;
  GPR_ASSERT(reload_arg != nullptr);
  grpc_tls_key_materials_config* config = reload_arg->key_materials_config;
  /* config should contain either original key materials provided by
   * application or the key materials resulted from credential reload. */
  GPR_ASSERT(config != nullptr);
  // Use the key_materials_config instance resulted from either original
  // key materials or credential reload to create a client-side handshaker
  // factory.
  if (is_client) {
    tsi_ssl_client_handshaker_factory* handshaker_factory = nullptr;
    tsi_ssl_client_handshaker_options options;
    memset(&options, 0, sizeof(options));
    if (config->pem_root_certs() == nullptr) {
      options.pem_root_certs = handshaker->pem_root_certs;
      options.root_store = handshaker->root_store;
      if (options.pem_root_certs == nullptr) {
        gpr_log(GPR_ERROR, "Could not get default pem root certs.");
        return TSI_INTERNAL_ERROR;
      }
    } else {
      options.pem_root_certs = config->pem_root_certs();
    }
    bool has_key_cert_pair =
        config->pem_key_cert_pairs() != nullptr &&
        config->pem_key_cert_pairs()->private_key != nullptr &&
        config->pem_key_cert_pairs()->cert_chain != nullptr;
    if (has_key_cert_pair) {
      handshaker->key_cert_pairs = tsi_convert_grpc_to_tsi_cert_pairs(
          config->pem_key_cert_pairs(), config->num_key_cert_pairs());
      handshaker->num_key_cert_pairs = config->num_key_cert_pairs();
      options.pem_key_cert_pair = handshaker->key_cert_pairs;
    }
    options.alpn_protocols = handshaker->alpn_protocols;
    options.num_alpn_protocols = handshaker->num_alpn_protocols;
    options.cipher_suites = tsi_get_ssl_cipher_suites();
    if (handshaker->session_cache != nullptr) {
      options.session_cache = handshaker->session_cache;
    }
    tsi_result result = tsi_create_ssl_client_handshaker_factory_with_options(
        &options, &handshaker_factory);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
              tsi_result_to_string(result));
      return TSI_INTERNAL_ERROR;
    }
    tsi::SslSessionLRUCache* session_cache =
        handshaker->session_cache == nullptr
            ? nullptr
            : reinterpret_cast<tsi::SslSessionLRUCache*>(
                  handshaker->session_cache)
                  ->Ref()
                  .get();
    return populate_ssl_handshaker(handshaker, handshaker_factory->ssl_context,
                                   true /* is_client*/,
                                   handshaker->server_name_indication,
                                   &handshaker_factory->base, session_cache);
    // Use the key_materials_config instance resulted from either original
    // key materials or credential reload to create a server-side handshaker
    // factory.
  } else {
    handshaker->key_cert_pairs = tsi_convert_grpc_to_tsi_cert_pairs(
        config->pem_key_cert_pairs(), config->num_key_cert_pairs());
    handshaker->num_key_cert_pairs = config->num_key_cert_pairs();
    tsi_ssl_server_handshaker_factory* handshaker_factory = nullptr;
    GPR_ASSERT(handshaker->options != nullptr);
    tsi_ssl_server_handshaker_options options;
    memset(&options, 0, sizeof(options));
    options.pem_key_cert_pairs = handshaker->key_cert_pairs;
    options.num_key_cert_pairs = config->num_key_cert_pairs();
    options.pem_client_root_certs = config->pem_root_certs() == nullptr
                                        ? handshaker->pem_root_certs
                                        : config->pem_root_certs();
    options.client_certificate_request =
        tsi_get_tsi_client_certificate_request_type(
            handshaker->options->cert_request_type());
    options.cipher_suites = tsi_get_ssl_cipher_suites();
    options.alpn_protocols = handshaker->alpn_protocols;
    options.num_alpn_protocols = handshaker->num_alpn_protocols;
    if (handshaker->session_ticket_key != nullptr) {
      options.session_ticket_key = handshaker->session_ticket_key;
      options.session_ticket_key_size = handshaker->session_ticket_key_size;
    }
    tsi_result result = tsi_create_ssl_server_handshaker_factory_with_options(
        &options, &handshaker_factory);
    if (result != TSI_OK) {
      gpr_log(GPR_ERROR, "Handshaker factory creation failed with %s.",
              tsi_result_to_string(result));
      return TSI_INTERNAL_ERROR;
    }
    return populate_ssl_handshaker(
        handshaker, handshaker_factory->ssl_contexts[0], false /* is_client*/,
        nullptr, &handshaker_factory->base, nullptr);
  }
}

namespace grpc_core {
namespace internal {

void tls_tsi_handshaker_set_session_cache_for_testing(
    tsi_handshaker* handshaker, tsi_ssl_session_cache* session_cache) {
  GPR_ASSERT(handshaker != nullptr);
  tsi_ssl_handshaker* ssl_handshaker =
      reinterpret_cast<tsi_ssl_handshaker*>(handshaker);
  ssl_handshaker->session_cache = session_cache;
}

void tls_tsi_handshaker_set_session_ticket_key_for_testing(
    tsi_handshaker* handshaker, const char* session_ticket_key,
    size_t session_ticket_key_size) {
  GPR_ASSERT(handshaker != nullptr);
  tsi_ssl_handshaker* ssl_handshaker =
      reinterpret_cast<tsi_ssl_handshaker*>(handshaker);
  ssl_handshaker->session_ticket_key = session_ticket_key;
  ssl_handshaker->session_ticket_key_size = session_ticket_key_size;
}

void tls_tsi_handshaker_set_alpn_protocols_for_testing(
    tsi_handshaker* handshaker, const char** alpn_protocols,
    size_t num_alpn_protocols) {
  GPR_ASSERT(handshaker != nullptr);
  tsi_ssl_handshaker* ssl_handshaker =
      reinterpret_cast<tsi_ssl_handshaker*>(handshaker);
  if (ssl_handshaker->alpn_protocols != nullptr) {
    gpr_free(ssl_handshaker->alpn_protocols);
  }
  ssl_handshaker->num_alpn_protocols = 0;
  ssl_handshaker->alpn_protocols = nullptr;
  if (num_alpn_protocols != 0) {
    ssl_handshaker->num_alpn_protocols = num_alpn_protocols;
    ssl_handshaker->alpn_protocols = static_cast<const char**>(
        gpr_zalloc(sizeof(char*) * num_alpn_protocols));
    for (size_t ind = 0; ind < num_alpn_protocols; ind++) {
      ssl_handshaker->alpn_protocols[ind] = alpn_protocols[ind];
    }
  }
}

void tls_tsi_handshaker_set_pem_root_for_testing(
    tsi_handshaker* handshaker, const char* pem_root_certs,
    tsi_ssl_root_certs_store* root_store) {
  GPR_ASSERT(handshaker != nullptr);
  tsi_ssl_handshaker* ssl_handshaker =
      reinterpret_cast<tsi_ssl_handshaker*>(handshaker);
  ssl_handshaker->pem_root_certs = pem_root_certs;
  ssl_handshaker->root_store = root_store;
}

grpc_tls_credential_reload_arg*
tls_tsi_handshaker_get_credential_reload_arg_for_testing(
    tsi_handshaker* handshaker) {
  GPR_ASSERT(handshaker != nullptr);
  tsi_ssl_handshaker* ssl_handshaker =
      reinterpret_cast<tsi_ssl_handshaker*>(handshaker);
  return ssl_handshaker->reload_arg;
}

}  // namespace internal
}  // namespace grpc_core
