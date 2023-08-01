//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include "src/core/tsi/ssl_transport_security.h"

#include <limits.h>
#include <string.h>

// TODO(jboeuf): refactor inet_ntop into a portability header.
// Note: for whomever reads this and tries to refactor this, this
// can't be in grpc, it has to be in gpr.
#ifdef GPR_WINDOWS
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include <string>

#include <openssl/bio.h>
#include <openssl/crypto.h>  // For OPENSSL_free
#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd_id.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/tsi/ssl/key_logging/ssl_key_logging.h"
#include "src/core/tsi/ssl/session_cache/ssl_session_cache.h"
#include "src/core/tsi/ssl_transport_security_utils.h"
#include "src/core/tsi/ssl_types.h"
#include "src/core/tsi/transport_security.h"

// --- Constants. ---

#define TSI_SSL_MAX_BIO_WRITE_ATTEMPTS 100
#define TSI_SSL_MAX_PROTECTED_FRAME_SIZE_UPPER_BOUND 16384
#define TSI_SSL_MAX_PROTECTED_FRAME_SIZE_LOWER_BOUND 1024
#define TSI_SSL_HANDSHAKER_OUTGOING_BUFFER_INITIAL_SIZE 1024

// Putting a macro like this and littering the source file with #if is really
// bad practice.
// TODO(jboeuf): refactor all the #if / #endif in a separate module.
#ifndef TSI_OPENSSL_ALPN_SUPPORT
#define TSI_OPENSSL_ALPN_SUPPORT 1
#endif

// TODO(jboeuf): I have not found a way to get this number dynamically from the
// SSL structure. This is what we would ultimately want though...
#define TSI_SSL_MAX_PROTECTION_OVERHEAD 100

using TlsSessionKeyLogger = tsi::TlsSessionKeyLoggerCache::TlsSessionKeyLogger;

// --- Structure definitions. ---

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
  grpc_core::RefCountedPtr<TlsSessionKeyLogger> key_logger;
};

struct tsi_ssl_server_handshaker_factory {
  // Several contexts to support SNI.
  // The tsi_peer array contains the subject names of the server certificates
  // associated with the contexts at the same index.
  tsi_ssl_handshaker_factory base;
  SSL_CTX** ssl_contexts;
  tsi_peer* ssl_context_x509_subject_names;
  size_t ssl_context_count;
  unsigned char* alpn_protocol_list;
  size_t alpn_protocol_list_length;
  grpc_core::RefCountedPtr<TlsSessionKeyLogger> key_logger;
};

struct tsi_ssl_handshaker {
  tsi_handshaker base;
  SSL* ssl;
  BIO* network_io;
  tsi_result result;
  unsigned char* outgoing_bytes_buffer;
  size_t outgoing_bytes_buffer_size;
  tsi_ssl_handshaker_factory* factory_ref;
};
struct tsi_ssl_handshaker_result {
  tsi_handshaker_result base;
  SSL* ssl;
  BIO* network_io;
  unsigned char* unused_bytes;
  size_t unused_bytes_size;
};
struct tsi_ssl_frame_protector {
  tsi_frame_protector base;
  SSL* ssl;
  BIO* network_io;
  unsigned char* buffer;
  size_t buffer_size;
  size_t buffer_offset;
};
// --- Library Initialization. ---

static gpr_once g_init_openssl_once = GPR_ONCE_INIT;
static int g_ssl_ctx_ex_factory_index = -1;
static const unsigned char kSslSessionIdContext[] = {'g', 'r', 'p', 'c'};
static int g_ssl_ex_verified_root_cert_index = -1;
#if !defined(OPENSSL_IS_BORINGSSL) && !defined(OPENSSL_NO_ENGINE)
static const char kSslEnginePrefix[] = "engine:";
#endif
#if OPENSSL_VERSION_NUMBER >= 0x30000000
static const char kSslEcCurveName[] = "NID_X9_62_prime256v1";
#endif

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
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  OPENSSL_init_ssl(0, nullptr);
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

  g_ssl_ex_verified_root_cert_index =
      SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  GPR_ASSERT(g_ssl_ex_verified_root_cert_index != -1);
}

// --- Ssl utils. ---

// TODO(jboeuf): Remove when we are past the debugging phase with this code.
static void ssl_log_where_info(const SSL* ssl, int where, int flag,
                               const char* msg) {
  if ((where & flag) && GRPC_TRACE_FLAG_ENABLED(tsi_tracing_enabled)) {
    gpr_log(GPR_INFO, "%20.20s - %30.30s  - %5.10s", msg,
            SSL_state_string_long(ssl), SSL_state_string(ssl));
  }
}

// Used for debugging. TODO(jboeuf): Remove when code is mature enough.
static void ssl_info_callback(const SSL* ssl, int where, int ret) {
  if (ret == 0) {
    gpr_log(GPR_ERROR, "ssl_info_callback: error occurred.\n");
    return;
  }

  ssl_log_where_info(ssl, where, SSL_CB_LOOP, "LOOP");
  ssl_log_where_info(ssl, where, SSL_CB_HANDSHAKE_START, "HANDSHAKE START");
  ssl_log_where_info(ssl, where, SSL_CB_HANDSHAKE_DONE, "HANDSHAKE DONE");
}

// Returns 1 if name looks like an IP address, 0 otherwise.
// This is a very rough heuristic, and only handles IPv6 in hexadecimal form.
static int looks_like_ip_address(absl::string_view name) {
  size_t dot_count = 0;
  size_t num_size = 0;
  for (size_t i = 0; i < name.size(); ++i) {
    if (name[i] == ':') {
      // IPv6 Address in hexadecimal form, : is not allowed in DNS names.
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

// Gets the subject CN from an X509 cert.
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

// Gets the subject CN of an X509 cert as a tsi_peer_property.
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

// Gets the subject of an X509 cert as a tsi_peer_property.
static tsi_result peer_property_from_x509_subject(X509* cert,
                                                  tsi_peer_property* property,
                                                  bool is_verified_root_cert) {
  X509_NAME* subject_name = X509_get_subject_name(cert);
  if (subject_name == nullptr) {
    gpr_log(GPR_INFO, "Could not get subject name from certificate.");
    return TSI_NOT_FOUND;
  }
  BIO* bio = BIO_new(BIO_s_mem());
  X509_NAME_print_ex(bio, subject_name, 0, XN_FLAG_RFC2253);
  char* contents;
  long len = BIO_get_mem_data(bio, &contents);
  if (len < 0) {
    gpr_log(GPR_ERROR, "Could not get subject entry from certificate.");
    BIO_free(bio);
    return TSI_INTERNAL_ERROR;
  }
  tsi_result result;
  if (!is_verified_root_cert) {
    result = tsi_construct_string_peer_property(
        TSI_X509_SUBJECT_PEER_PROPERTY, contents, static_cast<size_t>(len),
        property);
  } else {
    result = tsi_construct_string_peer_property(
        TSI_X509_VERIFIED_ROOT_CERT_SUBECT_PEER_PROPERTY, contents,
        static_cast<size_t>(len), property);
  }
  BIO_free(bio);
  return result;
}

// Gets the X509 cert in PEM format as a tsi_peer_property.
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
      TSI_X509_PEM_CERT_PROPERTY, contents, static_cast<size_t>(len), property);
  BIO_free(bio);
  return result;
}

// Gets the subject SANs from an X509 cert as a tsi_peer_property.
static tsi_result add_subject_alt_names_properties_to_peer(
    tsi_peer* peer, GENERAL_NAMES* subject_alt_names,
    size_t subject_alt_name_count, int* current_insert_index) {
  size_t i;
  tsi_result result = TSI_OK;

  for (i = 0; i < subject_alt_name_count; i++) {
    GENERAL_NAME* subject_alt_name =
        sk_GENERAL_NAME_value(subject_alt_names, TSI_SIZE_AS_SIZE(i));
    if (subject_alt_name->type == GEN_DNS ||
        subject_alt_name->type == GEN_EMAIL ||
        subject_alt_name->type == GEN_URI) {
      unsigned char* name = nullptr;
      int name_size;
      std::string property_name;
      if (subject_alt_name->type == GEN_DNS) {
        name_size = ASN1_STRING_to_UTF8(&name, subject_alt_name->d.dNSName);
        property_name = TSI_X509_DNS_PEER_PROPERTY;
      } else if (subject_alt_name->type == GEN_EMAIL) {
        name_size = ASN1_STRING_to_UTF8(&name, subject_alt_name->d.rfc822Name);
        property_name = TSI_X509_EMAIL_PEER_PROPERTY;
      } else {
        name_size = ASN1_STRING_to_UTF8(
            &name, subject_alt_name->d.uniformResourceIdentifier);
        property_name = TSI_X509_URI_PEER_PROPERTY;
      }
      if (name_size < 0) {
        gpr_log(GPR_ERROR, "Could not get utf8 from asn1 string.");
        result = TSI_INTERNAL_ERROR;
        break;
      }
      result = tsi_construct_string_peer_property(
          TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY,
          reinterpret_cast<const char*>(name), static_cast<size_t>(name_size),
          &peer->properties[(*current_insert_index)++]);
      if (result != TSI_OK) {
        OPENSSL_free(name);
        break;
      }
      result = tsi_construct_string_peer_property(
          property_name.c_str(), reinterpret_cast<const char*>(name),
          static_cast<size_t>(name_size),
          &peer->properties[(*current_insert_index)++]);
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
          &peer->properties[(*current_insert_index)++]);
      if (result != TSI_OK) break;
      result = tsi_construct_string_peer_property_from_cstring(
          TSI_X509_IP_PEER_PROPERTY, name,
          &peer->properties[(*current_insert_index)++]);
    } else {
      result = tsi_construct_string_peer_property_from_cstring(
          TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, "other types of SAN",
          &peer->properties[(*current_insert_index)++]);
    }
    if (result != TSI_OK) break;
  }
  return result;
}

// Gets information about the peer's X509 cert as a tsi_peer object.
static tsi_result peer_from_x509(X509* cert, int include_certificate_type,
                                 tsi_peer* peer) {
  // TODO(jboeuf): Maybe add more properties.
  GENERAL_NAMES* subject_alt_names = static_cast<GENERAL_NAMES*>(
      X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
  int subject_alt_name_count =
      (subject_alt_names != nullptr)
          ? static_cast<int>(sk_GENERAL_NAME_num(subject_alt_names))
          : 0;
  size_t property_count;
  tsi_result result;
  GPR_ASSERT(subject_alt_name_count >= 0);
  property_count = (include_certificate_type ? size_t{1} : 0) +
                   3 /* subject, common name, certificate */ +
                   static_cast<size_t>(subject_alt_name_count);
  for (int i = 0; i < subject_alt_name_count; i++) {
    GENERAL_NAME* subject_alt_name =
        sk_GENERAL_NAME_value(subject_alt_names, TSI_SIZE_AS_SIZE(i));
    // TODO(zhenlian): Clean up tsi_peer to avoid duplicate entries.
    // URI, DNS, email and ip address SAN fields are plumbed to tsi_peer, in
    // addition to all SAN fields (results in duplicate values). This code
    // snippet updates property_count accordingly.
    if (subject_alt_name->type == GEN_URI ||
        subject_alt_name->type == GEN_DNS ||
        subject_alt_name->type == GEN_EMAIL ||
        subject_alt_name->type == GEN_IPADD) {
      property_count += 1;
    }
  }
  result = tsi_construct_peer(property_count, peer);
  if (result != TSI_OK) return result;
  int current_insert_index = 0;
  do {
    if (include_certificate_type) {
      result = tsi_construct_string_peer_property_from_cstring(
          TSI_CERTIFICATE_TYPE_PEER_PROPERTY, TSI_X509_CERTIFICATE_TYPE,
          &peer->properties[current_insert_index++]);
      if (result != TSI_OK) break;
    }

    result = peer_property_from_x509_subject(
        cert, &peer->properties[current_insert_index++],
        /*is_verified_root_cert=*/false);
    if (result != TSI_OK) break;

    result = peer_property_from_x509_common_name(
        cert, &peer->properties[current_insert_index++]);
    if (result != TSI_OK) break;

    result =
        add_pem_certificate(cert, &peer->properties[current_insert_index++]);
    if (result != TSI_OK) break;

    if (subject_alt_name_count != 0) {
      result = add_subject_alt_names_properties_to_peer(
          peer, subject_alt_names, static_cast<size_t>(subject_alt_name_count),
          &current_insert_index);
      if (result != TSI_OK) break;
    }
  } while (false);

  if (subject_alt_names != nullptr) {
    sk_GENERAL_NAME_pop_free(subject_alt_names, GENERAL_NAME_free);
  }
  if (result != TSI_OK) tsi_peer_destruct(peer);

  GPR_ASSERT((int)peer->property_count == current_insert_index);
  return result;
}

// Loads an in-memory PEM certificate chain into the SSL context.
static tsi_result ssl_ctx_use_certificate_chain(SSL_CTX* context,
                                                const char* pem_cert_chain,
                                                size_t pem_cert_chain_size) {
  tsi_result result = TSI_OK;
  X509* certificate = nullptr;
  BIO* pem;
  GPR_ASSERT(pem_cert_chain_size <= INT_MAX);
  pem = BIO_new_mem_buf(pem_cert_chain, static_cast<int>(pem_cert_chain_size));
  if (pem == nullptr) return TSI_OUT_OF_RESOURCES;

  do {
    certificate =
        PEM_read_bio_X509_AUX(pem, nullptr, nullptr, const_cast<char*>(""));
    if (certificate == nullptr) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    if (!SSL_CTX_use_certificate(context, certificate)) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    while (true) {
      X509* certificate_authority =
          PEM_read_bio_X509(pem, nullptr, nullptr, const_cast<char*>(""));
      if (certificate_authority == nullptr) {
        ERR_clear_error();
        break;  // Done reading.
      }
      if (!SSL_CTX_add_extra_chain_cert(context, certificate_authority)) {
        X509_free(certificate_authority);
        result = TSI_INVALID_ARGUMENT;
        break;
      }
      // We don't need to free certificate_authority as its ownership has been
      // transferred to the context. That is not the case for certificate
      // though.
      //
    }
  } while (false);

  if (certificate != nullptr) X509_free(certificate);
  BIO_free(pem);
  return result;
}

#if !defined(OPENSSL_IS_BORINGSSL) && !defined(OPENSSL_NO_ENGINE)
static tsi_result ssl_ctx_use_engine_private_key(SSL_CTX* context,
                                                 const char* pem_key,
                                                 size_t pem_key_size) {
  tsi_result result = TSI_OK;
  EVP_PKEY* private_key = nullptr;
  ENGINE* engine = nullptr;
  char* engine_name = nullptr;
  // Parse key which is in following format engine:<engine_id>:<key_id>
  do {
    char* engine_start = (char*)pem_key + strlen(kSslEnginePrefix);
    char* engine_end = (char*)strchr(engine_start, ':');
    if (engine_end == nullptr) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    char* key_id = engine_end + 1;
    int engine_name_length = engine_end - engine_start;
    if (engine_name_length == 0) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    engine_name = static_cast<char*>(gpr_zalloc(engine_name_length + 1));
    memcpy(engine_name, engine_start, engine_name_length);
    gpr_log(GPR_DEBUG, "ENGINE key: %s", engine_name);
    ENGINE_load_dynamic();
    engine = ENGINE_by_id(engine_name);
    if (engine == nullptr) {
      // If not available at ENGINE_DIR, use dynamic to load from
      // current working directory.
      engine = ENGINE_by_id("dynamic");
      if (engine == nullptr) {
        gpr_log(GPR_ERROR, "Cannot load dynamic engine");
        result = TSI_INVALID_ARGUMENT;
        break;
      }
      if (!ENGINE_ctrl_cmd_string(engine, "ID", engine_name, 0) ||
          !ENGINE_ctrl_cmd_string(engine, "DIR_LOAD", "2", 0) ||
          !ENGINE_ctrl_cmd_string(engine, "DIR_ADD", ".", 0) ||
          !ENGINE_ctrl_cmd_string(engine, "LIST_ADD", "1", 0) ||
          !ENGINE_ctrl_cmd_string(engine, "LOAD", NULL, 0)) {
        gpr_log(GPR_ERROR, "Cannot find engine");
        result = TSI_INVALID_ARGUMENT;
        break;
      }
    }
    if (!ENGINE_set_default(engine, ENGINE_METHOD_ALL)) {
      gpr_log(GPR_ERROR, "ENGINE_set_default with ENGINE_METHOD_ALL failed");
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    if (!ENGINE_init(engine)) {
      gpr_log(GPR_ERROR, "ENGINE_init failed");
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    private_key = ENGINE_load_private_key(engine, key_id, 0, 0);
    if (private_key == nullptr) {
      gpr_log(GPR_ERROR, "ENGINE_load_private_key failed");
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    if (!SSL_CTX_use_PrivateKey(context, private_key)) {
      gpr_log(GPR_ERROR, "SSL_CTX_use_PrivateKey failed");
      result = TSI_INVALID_ARGUMENT;
      break;
    }
  } while (0);
  if (engine != nullptr) ENGINE_free(engine);
  if (private_key != nullptr) EVP_PKEY_free(private_key);
  if (engine_name != nullptr) gpr_free(engine_name);
  return result;
}
#endif  // !defined(OPENSSL_IS_BORINGSSL) && !defined(OPENSSL_NO_ENGINE)

static tsi_result ssl_ctx_use_pem_private_key(SSL_CTX* context,
                                              const char* pem_key,
                                              size_t pem_key_size) {
  tsi_result result = TSI_OK;
  EVP_PKEY* private_key = nullptr;
  BIO* pem;
  GPR_ASSERT(pem_key_size <= INT_MAX);
  pem = BIO_new_mem_buf(pem_key, static_cast<int>(pem_key_size));
  if (pem == nullptr) return TSI_OUT_OF_RESOURCES;
  do {
    private_key =
        PEM_read_bio_PrivateKey(pem, nullptr, nullptr, const_cast<char*>(""));
    if (private_key == nullptr) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
    if (!SSL_CTX_use_PrivateKey(context, private_key)) {
      result = TSI_INVALID_ARGUMENT;
      break;
    }
  } while (false);
  if (private_key != nullptr) EVP_PKEY_free(private_key);
  BIO_free(pem);
  return result;
}

// Loads an in-memory PEM private key into the SSL context.
static tsi_result ssl_ctx_use_private_key(SSL_CTX* context, const char* pem_key,
                                          size_t pem_key_size) {
// BoringSSL does not have ENGINE support
#if !defined(OPENSSL_IS_BORINGSSL) && !defined(OPENSSL_NO_ENGINE)
  if (strncmp(pem_key, kSslEnginePrefix, strlen(kSslEnginePrefix)) == 0) {
    return ssl_ctx_use_engine_private_key(context, pem_key, pem_key_size);
  } else
#endif  // !defined(OPENSSL_IS_BORINGSSL) && !defined(OPENSSL_NO_ENGINE)
  {
    return ssl_ctx_use_pem_private_key(context, pem_key, pem_key_size);
  }
}

// Loads in-memory PEM verification certs into the SSL context and optionally
// returns the verification cert names (root_names can be NULL).
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
  pem = BIO_new_mem_buf(pem_roots, static_cast<int>(pem_roots_size));
  if (cert_store == nullptr) return TSI_INVALID_ARGUMENT;
  if (pem == nullptr) return TSI_OUT_OF_RESOURCES;
  if (root_names != nullptr) {
    *root_names = sk_X509_NAME_new_null();
    if (*root_names == nullptr) return TSI_OUT_OF_RESOURCES;
  }

  while (true) {
    root = PEM_read_bio_X509_AUX(pem, nullptr, nullptr, const_cast<char*>(""));
    if (root == nullptr) {
      ERR_clear_error();
      break;  // We're at the end of stream.
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
    ERR_clear_error();
    if (!X509_STORE_add_cert(cert_store, root)) {
      unsigned long error = ERR_get_error();
      if (ERR_GET_LIB(error) != ERR_LIB_X509 ||
          ERR_GET_REASON(error) != X509_R_CERT_ALREADY_IN_HASH_TABLE) {
        gpr_log(GPR_ERROR, "Could not add root certificate to ssl context.");
        result = TSI_INTERNAL_ERROR;
        break;
      }
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
  X509_STORE_set_flags(cert_store,
                       X509_V_FLAG_PARTIAL_CHAIN | X509_V_FLAG_TRUSTED_FIRST);
  return x509_store_load_certs(cert_store, pem_roots, pem_roots_size,
                               root_name);
}

// Populates the SSL context with a private key and a cert chain, and sets the
// cipher list and the ephemeral ECDH key.
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
#if OPENSSL_VERSION_NUMBER < 0x30000000L
    EC_KEY* ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (!SSL_CTX_set_tmp_ecdh(context, ecdh)) {
      gpr_log(GPR_ERROR, "Could not set ephemeral ECDH key.");
      EC_KEY_free(ecdh);
      return TSI_INTERNAL_ERROR;
    }
    SSL_CTX_set_options(context, SSL_OP_SINGLE_ECDH_USE);
    EC_KEY_free(ecdh);
#else
    if (!SSL_CTX_set1_groups(context, kSslEcCurveName, 1)) {
      gpr_log(GPR_ERROR, "Could not set ephemeral ECDH key.");
      return TSI_INTERNAL_ERROR;
    }
    SSL_CTX_set_options(context, SSL_OP_SINGLE_ECDH_USE);
#endif
  }
  return TSI_OK;
}

// Extracts the CN and the SANs from an X509 cert as a peer object.
tsi_result tsi_ssl_extract_x509_subject_names_from_pem_cert(
    const char* pem_cert, tsi_peer* peer) {
  tsi_result result = TSI_OK;
  X509* cert = nullptr;
  BIO* pem;
  pem = BIO_new_mem_buf(pem_cert, static_cast<int>(strlen(pem_cert)));
  if (pem == nullptr) return TSI_OUT_OF_RESOURCES;

  cert = PEM_read_bio_X509(pem, nullptr, nullptr, const_cast<char*>(""));
  if (cert == nullptr) {
    gpr_log(GPR_ERROR, "Invalid certificate");
    result = TSI_INVALID_ARGUMENT;
  } else {
    result = peer_from_x509(cert, 0, peer);
  }
  if (cert != nullptr) X509_free(cert);
  BIO_free(pem);
  return result;
}

// Builds the alpn protocol name list according to rfc 7301.
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
    *(current++) = static_cast<uint8_t>(length);  // max checked above.
    memcpy(current, alpn_protocols[i], length);
    current += length;
  }
  // Safety check.
  if ((current < *protocol_name_list) ||
      (static_cast<uintptr_t>(current - *protocol_name_list) !=
       *protocol_name_list_length)) {
    return TSI_INTERNAL_ERROR;
  }
  return TSI_OK;
}

// This callback is invoked when the CRL has been verified and will soft-fail
// errors in verification depending on certain error types.
static int verify_cb(int ok, X509_STORE_CTX* ctx) {
  int cert_error = X509_STORE_CTX_get_error(ctx);
  if (cert_error == X509_V_ERR_UNABLE_TO_GET_CRL) {
    gpr_log(
        GPR_INFO,
        "Certificate verification failed to get CRL files. Ignoring error.");
    return 1;
  }
  if (cert_error != 0) {
    gpr_log(GPR_ERROR, "Certificate verify failed with code %d", cert_error);
  }
  return ok;
}

// The verification callback is used for clients that don't really care about
// the server's certificate, but we need to pull it anyway, in case a higher
// layer wants to look at it. In this case the verification may fail, but
// we don't really care.
static int NullVerifyCallback(int /*preverify_ok*/, X509_STORE_CTX* /*ctx*/) {
  return 1;
}

static int RootCertExtractCallback(int preverify_ok, X509_STORE_CTX* ctx) {
  if (ctx == nullptr) {
    return preverify_ok;
  }

  // There's a case where this function is set in SSL_CTX_set_verify and a CRL
  // related callback is set with X509_STORE_set_verify_cb. They overlap and
  // this will take precedence, thus we need to ensure the CRL related callback
  // is still called
  X509_VERIFY_PARAM* param = X509_STORE_CTX_get0_param(ctx);
  auto flags = X509_VERIFY_PARAM_get_flags(param);
  if (flags & X509_V_FLAG_CRL_CHECK) {
    preverify_ok = verify_cb(preverify_ok, ctx);
  }

  // If preverify_ok == 0, verification failed. We shouldn't expect to have a
  // verified chain, so there is no need to attempt to extract the root cert
  // from it
  if (preverify_ok == 0) {
    return preverify_ok;
  }

  // If we're here, verification was successful
  // Get the verified chain from the X509_STORE_CTX and put it on the SSL object
  // so that we have access to it when populating the tsi_peer
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  STACK_OF(X509)* chain = X509_STORE_CTX_get0_chain(ctx);
#else
  STACK_OF(X509)* chain = X509_STORE_CTX_get_chain(ctx);
#endif

  if (chain == nullptr) {
    return preverify_ok;
  }

  // The root cert is the last in the chain
  size_t chain_length = sk_X509_num(chain);
  if (chain_length == 0) {
    return preverify_ok;
  }
  X509* root_cert = sk_X509_value(chain, chain_length - 1);
  if (root_cert == nullptr) {
    return preverify_ok;
  }

  SSL* ssl = static_cast<SSL*>(
      X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
  if (ssl == nullptr) {
    return preverify_ok;
  }
  int success =
      SSL_set_ex_data(ssl, g_ssl_ex_verified_root_cert_index, root_cert);
  if (success == 0) {
    gpr_log(GPR_INFO, "Could not set verified root cert in SSL's ex_data");
  }
  return preverify_ok;
}

// Sets the min and max TLS version of |ssl_context| to |min_tls_version| and
// |max_tls_version|, respectively. Calling this method is a no-op when using
// OpenSSL versions < 1.1.
static tsi_result tsi_set_min_and_max_tls_versions(
    SSL_CTX* ssl_context, tsi_tls_version min_tls_version,
    tsi_tls_version max_tls_version) {
  if (ssl_context == nullptr) {
    gpr_log(GPR_INFO,
            "Invalid nullptr argument to |tsi_set_min_and_max_tls_versions|.");
    return TSI_INVALID_ARGUMENT;
  }
#if OPENSSL_VERSION_NUMBER >= 0x10100000
  // Set the min TLS version of the SSL context if using OpenSSL version
  // >= 1.1.0. This OpenSSL version is required because the
  // |SSL_CTX_set_min_proto_version| and |SSL_CTX_set_max_proto_version| APIs
  // only exist in this version range.
  switch (min_tls_version) {
    case tsi_tls_version::TSI_TLS1_2:
      SSL_CTX_set_min_proto_version(ssl_context, TLS1_2_VERSION);
      break;
#if defined(TLS1_3_VERSION)
    // If the library does not support TLS 1.3 and the caller requests a minimum
    // of TLS 1.3, then return an error because the caller's request cannot be
    // satisfied.
    case tsi_tls_version::TSI_TLS1_3:
      SSL_CTX_set_min_proto_version(ssl_context, TLS1_3_VERSION);
      break;
#endif
    default:
      gpr_log(GPR_INFO, "TLS version is not supported.");
      return TSI_FAILED_PRECONDITION;
  }

  // Set the max TLS version of the SSL context.
  switch (max_tls_version) {
    case tsi_tls_version::TSI_TLS1_2:
      SSL_CTX_set_max_proto_version(ssl_context, TLS1_2_VERSION);
      break;
    case tsi_tls_version::TSI_TLS1_3:
#if defined(TLS1_3_VERSION)
      SSL_CTX_set_max_proto_version(ssl_context, TLS1_3_VERSION);
#else
      // If the library does not support TLS 1.3, then set the max TLS version
      // to TLS 1.2 instead.
      SSL_CTX_set_max_proto_version(ssl_context, TLS1_2_VERSION);
#endif
      break;
    default:
      gpr_log(GPR_INFO, "TLS version is not supported.");
      return TSI_FAILED_PRECONDITION;
  }
#endif
  return TSI_OK;
}

// --- tsi_ssl_root_certs_store methods implementation. ---

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

// --- tsi_ssl_session_cache methods implementation. ---

tsi_ssl_session_cache* tsi_ssl_session_cache_create_lru(size_t capacity) {
  // Pointer will be dereferenced by unref call.
  return tsi::SslSessionLRUCache::Create(capacity).release()->c_ptr();
}

void tsi_ssl_session_cache_ref(tsi_ssl_session_cache* cache) {
  // Pointer will be dereferenced by unref call.
  tsi::SslSessionLRUCache::FromC(cache)->Ref().release();
}

void tsi_ssl_session_cache_unref(tsi_ssl_session_cache* cache) {
  tsi::SslSessionLRUCache::FromC(cache)->Unref();
}

// --- tsi_frame_protector methods implementation. ---

static tsi_result ssl_protector_protect(tsi_frame_protector* self,
                                        const unsigned char* unprotected_bytes,
                                        size_t* unprotected_bytes_size,
                                        unsigned char* protected_output_frames,
                                        size_t* protected_output_frames_size) {
  tsi_ssl_frame_protector* impl =
      reinterpret_cast<tsi_ssl_frame_protector*>(self);

  return grpc_core::SslProtectorProtect(
      unprotected_bytes, impl->buffer_size, impl->buffer_offset, impl->buffer,
      impl->ssl, impl->network_io, unprotected_bytes_size,
      protected_output_frames, protected_output_frames_size);
}

static tsi_result ssl_protector_protect_flush(
    tsi_frame_protector* self, unsigned char* protected_output_frames,
    size_t* protected_output_frames_size, size_t* still_pending_size) {
  tsi_ssl_frame_protector* impl =
      reinterpret_cast<tsi_ssl_frame_protector*>(self);
  return grpc_core::SslProtectorProtectFlush(
      impl->buffer_offset, impl->buffer, impl->ssl, impl->network_io,
      protected_output_frames, protected_output_frames_size,
      still_pending_size);
}

static tsi_result ssl_protector_unprotect(
    tsi_frame_protector* self, const unsigned char* protected_frames_bytes,
    size_t* protected_frames_bytes_size, unsigned char* unprotected_bytes,
    size_t* unprotected_bytes_size) {
  tsi_ssl_frame_protector* impl =
      reinterpret_cast<tsi_ssl_frame_protector*>(self);
  return grpc_core::SslProtectorUnprotect(
      protected_frames_bytes, impl->ssl, impl->network_io,
      protected_frames_bytes_size, unprotected_bytes, unprotected_bytes_size);
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

// --- tsi_server_handshaker_factory methods implementation. ---

static void tsi_ssl_handshaker_factory_destroy(
    tsi_ssl_handshaker_factory* factory) {
  if (factory == nullptr) return;

  if (factory->vtable != nullptr && factory->vtable->destroy != nullptr) {
    factory->vtable->destroy(factory);
  }
  // Note, we don't free(self) here because this object is always directly
  // embedded in another object. If tsi_ssl_handshaker_factory_init allocates
  // any memory, it should be free'd here.
}

static tsi_ssl_handshaker_factory* tsi_ssl_handshaker_factory_ref(
    tsi_ssl_handshaker_factory* factory) {
  if (factory == nullptr) return nullptr;
  gpr_refn(&factory->refcount, 1);
  return factory;
}

static void tsi_ssl_handshaker_factory_unref(
    tsi_ssl_handshaker_factory* factory) {
  if (factory == nullptr) return;

  if (gpr_unref(&factory->refcount)) {
    tsi_ssl_handshaker_factory_destroy(factory);
  }
}

static tsi_ssl_handshaker_factory_vtable handshaker_factory_vtable = {nullptr};

// Initializes a tsi_ssl_handshaker_factory object. Caller is responsible for
// allocating memory for the factory.
static void tsi_ssl_handshaker_factory_init(
    tsi_ssl_handshaker_factory* factory) {
  GPR_ASSERT(factory != nullptr);

  factory->vtable = &handshaker_factory_vtable;
  gpr_ref_init(&factory->refcount, 1);
}

// Gets the X509 cert chain in PEM format as a tsi_peer_property.
tsi_result tsi_ssl_get_cert_chain_contents(STACK_OF(X509) * peer_chain,
                                           tsi_peer_property* property) {
  BIO* bio = BIO_new(BIO_s_mem());
  const auto peer_chain_len = sk_X509_num(peer_chain);
  for (auto i = decltype(peer_chain_len){0}; i < peer_chain_len; i++) {
    if (!PEM_write_bio_X509(bio, sk_X509_value(peer_chain, i))) {
      BIO_free(bio);
      return TSI_INTERNAL_ERROR;
    }
  }
  char* contents;
  long len = BIO_get_mem_data(bio, &contents);
  if (len <= 0) {
    BIO_free(bio);
    return TSI_INTERNAL_ERROR;
  }
  tsi_result result = tsi_construct_string_peer_property(
      TSI_X509_PEM_CERT_CHAIN_PROPERTY, contents, static_cast<size_t>(len),
      property);
  BIO_free(bio);
  return result;
}

// --- tsi_handshaker_result methods implementation. ---
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
#if TSI_OPENSSL_ALPN_SUPPORT
  SSL_get0_alpn_selected(impl->ssl, &alpn_selected, &alpn_selected_len);
#endif  // TSI_OPENSSL_ALPN_SUPPORT
  if (alpn_selected == nullptr) {
    // Try npn.
    SSL_get0_next_proto_negotiated(impl->ssl, &alpn_selected,
                                   &alpn_selected_len);
  }
  // When called on the client side, the stack also contains the
  // peer's certificate; When called on the server side,
  // the peer's certificate is not present in the stack
  STACK_OF(X509)* peer_chain = SSL_get_peer_cert_chain(impl->ssl);

  X509* verified_root_cert = static_cast<X509*>(
      SSL_get_ex_data(impl->ssl, g_ssl_ex_verified_root_cert_index));
  // 1 is for session reused property.
  size_t new_property_count = peer->property_count + 3;
  if (alpn_selected != nullptr) new_property_count++;
  if (peer_chain != nullptr) new_property_count++;
  if (verified_root_cert != nullptr) new_property_count++;
  tsi_peer_property* new_properties = static_cast<tsi_peer_property*>(
      gpr_zalloc(sizeof(*new_properties) * new_property_count));
  for (size_t i = 0; i < peer->property_count; i++) {
    new_properties[i] = peer->properties[i];
  }
  if (peer->properties != nullptr) gpr_free(peer->properties);
  peer->properties = new_properties;
  // Add peer chain if available
  if (peer_chain != nullptr) {
    result = tsi_ssl_get_cert_chain_contents(
        peer_chain, &peer->properties[peer->property_count]);
    if (result == TSI_OK) peer->property_count++;
  }
  if (alpn_selected != nullptr) {
    result = tsi_construct_string_peer_property(
        TSI_SSL_ALPN_SELECTED_PROTOCOL,
        reinterpret_cast<const char*>(alpn_selected), alpn_selected_len,
        &peer->properties[peer->property_count]);
    if (result != TSI_OK) return result;
    peer->property_count++;
  }
  // Add security_level peer property.
  result = tsi_construct_string_peer_property_from_cstring(
      TSI_SECURITY_LEVEL_PEER_PROPERTY,
      tsi_security_level_to_string(TSI_PRIVACY_AND_INTEGRITY),
      &peer->properties[peer->property_count]);
  if (result != TSI_OK) return result;
  peer->property_count++;

  const char* session_reused = SSL_session_reused(impl->ssl) ? "true" : "false";
  result = tsi_construct_string_peer_property_from_cstring(
      TSI_SSL_SESSION_REUSED_PEER_PROPERTY, session_reused,
      &peer->properties[peer->property_count]);
  if (result != TSI_OK) return result;
  peer->property_count++;

  if (verified_root_cert != nullptr) {
    result = peer_property_from_x509_subject(
        verified_root_cert, &peer->properties[peer->property_count], true);
    if (result != TSI_OK) {
      gpr_log(GPR_DEBUG,
              "Problem extracting subject from verified_root_cert. result: %d",
              static_cast<int>(result));
    }
    peer->property_count++;
  }

  return result;
}

static tsi_result ssl_handshaker_result_get_frame_protector_type(
    const tsi_handshaker_result* /*self*/,
    tsi_frame_protector_type* frame_protector_type) {
  *frame_protector_type = TSI_FRAME_PROTECTOR_NORMAL;
  return TSI_OK;
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

  // Transfer ownership of ssl and network_io to the frame protector.
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
    ssl_handshaker_result_get_frame_protector_type,
    nullptr,  // create_zero_copy_grpc_protector
    ssl_handshaker_result_create_frame_protector,
    ssl_handshaker_result_get_unused_bytes,
    ssl_handshaker_result_destroy,
};

static tsi_result ssl_handshaker_result_create(
    tsi_ssl_handshaker* handshaker, unsigned char* unused_bytes,
    size_t unused_bytes_size, tsi_handshaker_result** handshaker_result,
    std::string* error) {
  if (handshaker == nullptr || handshaker_result == nullptr ||
      (unused_bytes_size > 0 && unused_bytes == nullptr)) {
    if (error != nullptr) *error = "invalid argument";
    return TSI_INVALID_ARGUMENT;
  }
  tsi_ssl_handshaker_result* result =
      grpc_core::Zalloc<tsi_ssl_handshaker_result>();
  result->base.vtable = &handshaker_result_vtable;
  // Transfer ownership of ssl and network_io to the handshaker result.
  result->ssl = handshaker->ssl;
  handshaker->ssl = nullptr;
  result->network_io = handshaker->network_io;
  handshaker->network_io = nullptr;
  // Transfer ownership of |unused_bytes| to the handshaker result.
  result->unused_bytes = unused_bytes;
  result->unused_bytes_size = unused_bytes_size;
  *handshaker_result = &result->base;
  return TSI_OK;
}

// --- tsi_handshaker methods implementation. ---

static tsi_result ssl_handshaker_get_bytes_to_send_to_peer(
    tsi_ssl_handshaker* impl, unsigned char* bytes, size_t* bytes_size,
    std::string* error) {
  int bytes_read_from_ssl = 0;
  if (bytes == nullptr || bytes_size == nullptr || *bytes_size > INT_MAX) {
    if (error != nullptr) *error = "invalid argument";
    return TSI_INVALID_ARGUMENT;
  }
  GPR_ASSERT(*bytes_size <= INT_MAX);
  bytes_read_from_ssl =
      BIO_read(impl->network_io, bytes, static_cast<int>(*bytes_size));
  if (bytes_read_from_ssl < 0) {
    *bytes_size = 0;
    if (!BIO_should_retry(impl->network_io)) {
      if (error != nullptr) *error = "error reading from BIO";
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

static tsi_result ssl_handshaker_do_handshake(tsi_ssl_handshaker* impl,
                                              std::string* error) {
  if (ssl_handshaker_get_result(impl) != TSI_HANDSHAKE_IN_PROGRESS) {
    impl->result = TSI_OK;
    return impl->result;
  } else {
    ERR_clear_error();
    // Get ready to get some bytes from SSL.
    int ssl_result = SSL_do_handshake(impl->ssl);
    ssl_result = SSL_get_error(impl->ssl, ssl_result);
    switch (ssl_result) {
      case SSL_ERROR_WANT_READ:
        if (BIO_pending(impl->network_io) == 0) {
          // We need more data.
          return TSI_INCOMPLETE_DATA;
        } else {
          return TSI_OK;
        }
      case SSL_ERROR_NONE:
        return TSI_OK;
      case SSL_ERROR_WANT_WRITE:
        return TSI_DRAIN_BUFFER;
      default: {
        char err_str[256];
        ERR_error_string_n(ERR_get_error(), err_str, sizeof(err_str));
        gpr_log(GPR_ERROR, "Handshake failed with fatal error %s: %s.",
                grpc_core::SslErrorString(ssl_result), err_str);
        if (error != nullptr) {
          *error = absl::StrCat(grpc_core::SslErrorString(ssl_result), ": ",
                                err_str);
        }
        impl->result = TSI_PROTOCOL_FAILURE;
        return impl->result;
      }
    }
  }
}

static tsi_result ssl_handshaker_process_bytes_from_peer(
    tsi_ssl_handshaker* impl, const unsigned char* bytes, size_t* bytes_size,
    std::string* error) {
  int bytes_written_into_ssl_size = 0;
  if (bytes == nullptr || bytes_size == nullptr || *bytes_size > INT_MAX) {
    if (error != nullptr) *error = "invalid argument";
    return TSI_INVALID_ARGUMENT;
  }
  GPR_ASSERT(*bytes_size <= INT_MAX);
  bytes_written_into_ssl_size =
      BIO_write(impl->network_io, bytes, static_cast<int>(*bytes_size));
  if (bytes_written_into_ssl_size < 0) {
    gpr_log(GPR_ERROR, "Could not write to memory BIO.");
    if (error != nullptr) *error = "could not write to memory BIO";
    impl->result = TSI_INTERNAL_ERROR;
    return impl->result;
  }
  *bytes_size = static_cast<size_t>(bytes_written_into_ssl_size);
  return ssl_handshaker_do_handshake(impl, error);
}

static void ssl_handshaker_destroy(tsi_handshaker* self) {
  tsi_ssl_handshaker* impl = reinterpret_cast<tsi_ssl_handshaker*>(self);
  SSL_free(impl->ssl);
  BIO_free(impl->network_io);
  gpr_free(impl->outgoing_bytes_buffer);
  tsi_ssl_handshaker_factory_unref(impl->factory_ref);
  gpr_free(impl);
}

// Removes the bytes remaining in |impl->SSL|'s read BIO and writes them to
// |bytes_remaining|.
static tsi_result ssl_bytes_remaining(tsi_ssl_handshaker* impl,
                                      unsigned char** bytes_remaining,
                                      size_t* bytes_remaining_size,
                                      std::string* error) {
  if (impl == nullptr || bytes_remaining == nullptr ||
      bytes_remaining_size == nullptr) {
    if (error != nullptr) *error = "invalid argument";
    return TSI_INVALID_ARGUMENT;
  }
  // Atempt to read all of the bytes in SSL's read BIO. These bytes should
  // contain application data records that were appended to a handshake record
  // containing the ClientFinished or ServerFinished message.
  size_t bytes_in_ssl = BIO_pending(SSL_get_rbio(impl->ssl));
  if (bytes_in_ssl == 0) return TSI_OK;
  *bytes_remaining = static_cast<uint8_t*>(gpr_malloc(bytes_in_ssl));
  int bytes_read = BIO_read(SSL_get_rbio(impl->ssl), *bytes_remaining,
                            static_cast<int>(bytes_in_ssl));
  // If an unexpected number of bytes were read, return an error status and free
  // all of the bytes that were read.
  if (bytes_read < 0 || static_cast<size_t>(bytes_read) != bytes_in_ssl) {
    gpr_log(GPR_ERROR,
            "Failed to read the expected number of bytes from SSL object.");
    gpr_free(*bytes_remaining);
    *bytes_remaining = nullptr;
    if (error != nullptr) {
      *error = "Failed to read the expected number of bytes from SSL object.";
    }
    return TSI_INTERNAL_ERROR;
  }
  *bytes_remaining_size = static_cast<size_t>(bytes_read);
  return TSI_OK;
}

// Write handshake data received from SSL to an unbound output buffer.
// By doing that, we drain SSL bio buffer used to hold handshake data.
// This API needs to be repeatedly called until all handshake data are
// received from SSL.
static tsi_result ssl_handshaker_write_output_buffer(tsi_handshaker* self,
                                                     size_t* bytes_written,
                                                     std::string* error) {
  tsi_ssl_handshaker* impl = reinterpret_cast<tsi_ssl_handshaker*>(self);
  tsi_result status = TSI_OK;
  size_t offset = *bytes_written;
  do {
    size_t to_send_size = impl->outgoing_bytes_buffer_size - offset;
    status = ssl_handshaker_get_bytes_to_send_to_peer(
        impl, impl->outgoing_bytes_buffer + offset, &to_send_size, error);
    offset += to_send_size;
    if (status == TSI_INCOMPLETE_DATA) {
      impl->outgoing_bytes_buffer_size *= 2;
      impl->outgoing_bytes_buffer = static_cast<unsigned char*>(gpr_realloc(
          impl->outgoing_bytes_buffer, impl->outgoing_bytes_buffer_size));
    }
  } while (status == TSI_INCOMPLETE_DATA);
  *bytes_written = offset;
  return status;
}

static tsi_result ssl_handshaker_next(tsi_handshaker* self,
                                      const unsigned char* received_bytes,
                                      size_t received_bytes_size,
                                      const unsigned char** bytes_to_send,
                                      size_t* bytes_to_send_size,
                                      tsi_handshaker_result** handshaker_result,
                                      tsi_handshaker_on_next_done_cb /*cb*/,
                                      void* /*user_data*/, std::string* error) {
  // Input sanity check.
  if ((received_bytes_size > 0 && received_bytes == nullptr) ||
      bytes_to_send == nullptr || bytes_to_send_size == nullptr ||
      handshaker_result == nullptr) {
    if (error != nullptr) *error = "invalid argument";
    return TSI_INVALID_ARGUMENT;
  }
  // If there are received bytes, process them first.
  tsi_ssl_handshaker* impl = reinterpret_cast<tsi_ssl_handshaker*>(self);
  tsi_result status = TSI_OK;
  size_t bytes_written = 0;
  if (received_bytes_size > 0) {
    unsigned char* remaining_bytes_to_write_to_openssl =
        const_cast<unsigned char*>(received_bytes);
    size_t remaining_bytes_to_write_to_openssl_size = received_bytes_size;
    size_t number_bio_write_attempts = 0;
    while (remaining_bytes_to_write_to_openssl_size > 0 &&
           (status == TSI_OK || status == TSI_INCOMPLETE_DATA) &&
           number_bio_write_attempts < TSI_SSL_MAX_BIO_WRITE_ATTEMPTS) {
      ++number_bio_write_attempts;
      // Try to write all of the remaining bytes to the BIO.
      size_t bytes_written_to_openssl =
          remaining_bytes_to_write_to_openssl_size;
      status = ssl_handshaker_process_bytes_from_peer(
          impl, remaining_bytes_to_write_to_openssl, &bytes_written_to_openssl,
          error);
      // As long as the BIO is full, drive the SSL handshake to consume bytes
      // from the BIO. If the SSL handshake returns any bytes, write them to the
      // peer.
      while (status == TSI_DRAIN_BUFFER) {
        status =
            ssl_handshaker_write_output_buffer(self, &bytes_written, error);
        if (status != TSI_OK) return status;
        status = ssl_handshaker_do_handshake(impl, error);
      }
      // Move the pointer to the first byte not yet successfully written to the
      // BIO.
      remaining_bytes_to_write_to_openssl_size -= bytes_written_to_openssl;
      remaining_bytes_to_write_to_openssl += bytes_written_to_openssl;
    }
  }
  if (status != TSI_OK) return status;
  // Get bytes to send to the peer, if available.
  status = ssl_handshaker_write_output_buffer(self, &bytes_written, error);
  if (status != TSI_OK) return status;
  *bytes_to_send = impl->outgoing_bytes_buffer;
  *bytes_to_send_size = bytes_written;
  // If handshake completes, create tsi_handshaker_result.
  if (ssl_handshaker_get_result(impl) == TSI_HANDSHAKE_IN_PROGRESS) {
    *handshaker_result = nullptr;
  } else {
    // Any bytes that remain in |impl->ssl|'s read BIO after the handshake is
    // complete must be extracted and set to the unused bytes of the handshaker
    // result. This indicates to the gRPC stack that there are bytes from the
    // peer that must be processed.
    unsigned char* unused_bytes = nullptr;
    size_t unused_bytes_size = 0;
    status =
        ssl_bytes_remaining(impl, &unused_bytes, &unused_bytes_size, error);
    if (status != TSI_OK) return status;
    if (unused_bytes_size > received_bytes_size) {
      gpr_log(GPR_ERROR, "More unused bytes than received bytes.");
      gpr_free(unused_bytes);
      if (error != nullptr) *error = "More unused bytes than received bytes.";
      return TSI_INTERNAL_ERROR;
    }
    status = ssl_handshaker_result_create(impl, unused_bytes, unused_bytes_size,
                                          handshaker_result, error);
    if (status == TSI_OK) {
      // Indicates that the handshake has completed and that a handshaker_result
      // has been created.
      self->handshaker_result_created = true;
    }
  }
  return status;
}

static const tsi_handshaker_vtable handshaker_vtable = {
    nullptr,  // get_bytes_to_send_to_peer -- deprecated
    nullptr,  // process_bytes_from_peer   -- deprecated
    nullptr,  // get_result                -- deprecated
    nullptr,  // extract_peer              -- deprecated
    nullptr,  // create_frame_protector    -- deprecated
    ssl_handshaker_destroy,
    ssl_handshaker_next,
    nullptr,  // shutdown
};

// --- tsi_ssl_handshaker_factory common methods. ---

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
                                            size_t network_bio_buf_size,
                                            size_t ssl_bio_buf_size,
                                            tsi_ssl_handshaker_factory* factory,
                                            tsi_handshaker** handshaker) {
  SSL* ssl = SSL_new(ctx);
  BIO* network_io = nullptr;
  BIO* ssl_io = nullptr;
  tsi_ssl_handshaker* impl = nullptr;
  *handshaker = nullptr;
  if (ctx == nullptr) {
    gpr_log(GPR_ERROR, "SSL Context is null. Should never happen.");
    return TSI_INTERNAL_ERROR;
  }
  if (ssl == nullptr) {
    return TSI_OUT_OF_RESOURCES;
  }
  SSL_set_info_callback(ssl, ssl_info_callback);

  if (!BIO_new_bio_pair(&network_io, network_bio_buf_size, &ssl_io,
                        ssl_bio_buf_size)) {
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
    tsi_ssl_client_handshaker_factory* client_factory =
        reinterpret_cast<tsi_ssl_client_handshaker_factory*>(factory);
    if (client_factory->session_cache != nullptr) {
      tsi_ssl_handshaker_resume_session(ssl,
                                        client_factory->session_cache.get());
    }
    ERR_clear_error();
    ssl_result = SSL_do_handshake(ssl);
    ssl_result = SSL_get_error(ssl, ssl_result);
    if (ssl_result != SSL_ERROR_WANT_READ) {
      gpr_log(GPR_ERROR,
              "Unexpected error received from first SSL_do_handshake call: %s",
              grpc_core::SslErrorString(ssl_result));
      SSL_free(ssl);
      BIO_free(network_io);
      return TSI_INTERNAL_ERROR;
    }
  } else {
    SSL_set_accept_state(ssl);
  }

  impl = grpc_core::Zalloc<tsi_ssl_handshaker>();
  impl->ssl = ssl;
  impl->network_io = network_io;
  impl->result = TSI_HANDSHAKE_IN_PROGRESS;
  impl->outgoing_bytes_buffer_size =
      TSI_SSL_HANDSHAKER_OUTGOING_BUFFER_INITIAL_SIZE;
  impl->outgoing_bytes_buffer =
      static_cast<unsigned char*>(gpr_zalloc(impl->outgoing_bytes_buffer_size));
  impl->base.vtable = &handshaker_vtable;
  impl->factory_ref = tsi_ssl_handshaker_factory_ref(factory);
  *handshaker = &impl->base;
  return TSI_OK;
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

// --- tsi_ssl_client_handshaker_factory methods implementation. ---

tsi_result tsi_ssl_client_handshaker_factory_create_handshaker(
    tsi_ssl_client_handshaker_factory* factory,
    const char* server_name_indication, size_t network_bio_buf_size,
    size_t ssl_bio_buf_size, tsi_handshaker** handshaker) {
  return create_tsi_ssl_handshaker(
      factory->ssl_context, 1, server_name_indication, network_bio_buf_size,
      ssl_bio_buf_size, &factory->base, handshaker);
}

void tsi_ssl_client_handshaker_factory_unref(
    tsi_ssl_client_handshaker_factory* factory) {
  if (factory == nullptr) return;
  tsi_ssl_handshaker_factory_unref(&factory->base);
}

static void tsi_ssl_client_handshaker_factory_destroy(
    tsi_ssl_handshaker_factory* factory) {
  if (factory == nullptr) return;
  tsi_ssl_client_handshaker_factory* self =
      reinterpret_cast<tsi_ssl_client_handshaker_factory*>(factory);
  if (self->ssl_context != nullptr) SSL_CTX_free(self->ssl_context);
  if (self->alpn_protocol_list != nullptr) gpr_free(self->alpn_protocol_list);
  self->session_cache.reset();
  self->key_logger.reset();
  gpr_free(self);
}

static int client_handshaker_factory_npn_callback(
    SSL* /*ssl*/, unsigned char** out, unsigned char* outlen,
    const unsigned char* in, unsigned int inlen, void* arg) {
  tsi_ssl_client_handshaker_factory* factory =
      static_cast<tsi_ssl_client_handshaker_factory*>(arg);
  return select_protocol_list(const_cast<const unsigned char**>(out), outlen,
                              factory->alpn_protocol_list,
                              factory->alpn_protocol_list_length, in, inlen);
}

// --- tsi_ssl_server_handshaker_factory methods implementation. ---

tsi_result tsi_ssl_server_handshaker_factory_create_handshaker(
    tsi_ssl_server_handshaker_factory* factory, size_t network_bio_buf_size,
    size_t ssl_bio_buf_size, tsi_handshaker** handshaker) {
  if (factory->ssl_context_count == 0) return TSI_INVALID_ARGUMENT;
  // Create the handshaker with the first context. We will switch if needed
  // because of SNI in ssl_server_handshaker_factory_servername_callback.
  return create_tsi_ssl_handshaker(factory->ssl_contexts[0], 0, nullptr,
                                   network_bio_buf_size, ssl_bio_buf_size,
                                   &factory->base, handshaker);
}

void tsi_ssl_server_handshaker_factory_unref(
    tsi_ssl_server_handshaker_factory* factory) {
  if (factory == nullptr) return;
  tsi_ssl_handshaker_factory_unref(&factory->base);
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
  self->key_logger.reset();
  gpr_free(self);
}

static int does_entry_match_name(absl::string_view entry,
                                 absl::string_view name) {
  if (entry.empty()) return 0;

  // Take care of '.' terminations.
  if (name.back() == '.') {
    name.remove_suffix(1);
  }
  if (entry.back() == '.') {
    entry.remove_suffix(1);
    if (entry.empty()) return 0;
  }

  if (absl::EqualsIgnoreCase(name, entry)) {
    return 1;  // Perfect match.
  }
  if (entry.front() != '*') return 0;

  // Wildchar subdomain matching.
  if (entry.size() < 3 || entry[1] != '.') {  // At least *.x
    gpr_log(GPR_ERROR, "Invalid wildchar entry.");
    return 0;
  }
  size_t name_subdomain_pos = name.find('.');
  if (name_subdomain_pos == absl::string_view::npos) return 0;
  if (name_subdomain_pos >= name.size() - 2) return 0;
  absl::string_view name_subdomain =
      name.substr(name_subdomain_pos + 1);  // Starts after the dot.
  entry.remove_prefix(2);                   // Remove *.
  size_t dot = name_subdomain.find('.');
  if (dot == absl::string_view::npos || dot == name_subdomain.size() - 1) {
    gpr_log(GPR_ERROR, "Invalid toplevel subdomain: %s",
            std::string(name_subdomain).c_str());
    return 0;
  }
  if (name_subdomain.back() == '.') {
    name_subdomain.remove_suffix(1);
  }
  return !entry.empty() && absl::EqualsIgnoreCase(name_subdomain, entry);
}

static int ssl_server_handshaker_factory_servername_callback(SSL* ssl,
                                                             int* /*ap*/,
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
  return SSL_TLSEXT_ERR_NOACK;
}

#if TSI_OPENSSL_ALPN_SUPPORT
static int server_handshaker_factory_alpn_callback(
    SSL* /*ssl*/, const unsigned char** out, unsigned char* outlen,
    const unsigned char* in, unsigned int inlen, void* arg) {
  tsi_ssl_server_handshaker_factory* factory =
      static_cast<tsi_ssl_server_handshaker_factory*>(arg);
  return select_protocol_list(out, outlen, in, inlen,
                              factory->alpn_protocol_list,
                              factory->alpn_protocol_list_length);
}
#endif  // TSI_OPENSSL_ALPN_SUPPORT

static int server_handshaker_factory_npn_advertised_callback(
    SSL* /*ssl*/, const unsigned char** out, unsigned int* outlen, void* arg) {
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
  // Return 1 to indicate transferred ownership over the given session.
  return 1;
}

/// This callback is invoked at client or server when ssl/tls handshakes
/// complete and keylogging is enabled.
template <typename T>
static void ssl_keylogging_callback(const SSL* ssl, const char* info) {
  SSL_CTX* ssl_context = SSL_get_SSL_CTX(ssl);
  GPR_ASSERT(ssl_context != nullptr);
  void* arg = SSL_CTX_get_ex_data(ssl_context, g_ssl_ctx_ex_factory_index);
  T* factory = static_cast<T*>(arg);
  factory->key_logger->LogSessionKeys(ssl_context, info);
}

// --- tsi_ssl_handshaker_factory constructors. ---

static tsi_ssl_handshaker_factory_vtable client_handshaker_factory_vtable = {
    tsi_ssl_client_handshaker_factory_destroy};

tsi_result tsi_create_ssl_client_handshaker_factory(
    const tsi_ssl_pem_key_cert_pair* pem_key_cert_pair,
    const char* pem_root_certs, const char* cipher_suites,
    const char** alpn_protocols, uint16_t num_alpn_protocols,
    tsi_ssl_client_handshaker_factory** factory) {
  tsi_ssl_client_handshaker_options options;
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

#if OPENSSL_VERSION_NUMBER >= 0x10100000
  ssl_context = SSL_CTX_new(TLS_method());
#else
  ssl_context = SSL_CTX_new(TLSv1_2_method());
#endif
  if (ssl_context == nullptr) {
    grpc_core::LogSslErrorStack();
    gpr_log(GPR_ERROR, "Could not create ssl context.");
    return TSI_INVALID_ARGUMENT;
  }

  result = tsi_set_min_and_max_tls_versions(
      ssl_context, options->min_tls_version, options->max_tls_version);
  if (result != TSI_OK) return result;

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
    SSL_CTX_sess_set_new_cb(ssl_context,
                            server_handshaker_factory_new_session_callback);
    SSL_CTX_set_session_cache_mode(ssl_context, SSL_SESS_CACHE_CLIENT);
  }

#if OPENSSL_VERSION_NUMBER >= 0x10101000 && !defined(LIBRESSL_VERSION_NUMBER)
  if (options->key_logger != nullptr) {
    impl->key_logger = options->key_logger->Ref();
    // SSL_CTX_set_keylog_callback is set here to register callback
    // when ssl/tls handshakes complete.
    SSL_CTX_set_keylog_callback(
        ssl_context,
        ssl_keylogging_callback<tsi_ssl_client_handshaker_factory>);
  }
#endif

  if (options->session_cache != nullptr || options->key_logger != nullptr) {
    // Need to set factory at g_ssl_ctx_ex_factory_index
    SSL_CTX_set_ex_data(ssl_context, g_ssl_ctx_ex_factory_index, impl);
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
#endif  // TSI_OPENSSL_ALPN_SUPPORT
      SSL_CTX_set_next_proto_select_cb(
          ssl_context, client_handshaker_factory_npn_callback, impl);
    }
  } while (false);
  if (result != TSI_OK) {
    tsi_ssl_handshaker_factory_unref(&impl->base);
    return result;
  }
  if (options->skip_server_certificate_verification) {
    SSL_CTX_set_verify(ssl_context, SSL_VERIFY_PEER, NullVerifyCallback);
  } else {
    SSL_CTX_set_verify(ssl_context, SSL_VERIFY_PEER, RootCertExtractCallback);
  }

#if OPENSSL_VERSION_NUMBER >= 0x10100000
  if (options->crl_directory != nullptr &&
      strcmp(options->crl_directory, "") != 0) {
    gpr_log(GPR_INFO, "enabling client CRL checking with path: %s",
            options->crl_directory);
    X509_STORE* cert_store = SSL_CTX_get_cert_store(ssl_context);
    X509_STORE_set_verify_cb(cert_store, verify_cb);
    if (!X509_STORE_load_locations(cert_store, nullptr,
                                   options->crl_directory)) {
      gpr_log(GPR_ERROR, "Failed to load CRL File from directory.");
    } else {
      X509_VERIFY_PARAM* param = X509_STORE_get0_param(cert_store);
      X509_VERIFY_PARAM_set_flags(
          param, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
      gpr_log(GPR_INFO, "enabled client side CRL checking.");
    }
  }
#endif

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

  if (options->key_logger != nullptr) {
    impl->key_logger = options->key_logger->Ref();
  }

  for (i = 0; i < options->num_key_cert_pairs; i++) {
    do {
#if OPENSSL_VERSION_NUMBER >= 0x10100000
      impl->ssl_contexts[i] = SSL_CTX_new(TLS_method());
#else
      impl->ssl_contexts[i] = SSL_CTX_new(TLSv1_2_method());
#endif
      if (impl->ssl_contexts[i] == nullptr) {
        grpc_core::LogSslErrorStack();
        gpr_log(GPR_ERROR, "Could not create ssl context.");
        result = TSI_OUT_OF_RESOURCES;
        break;
      }

      result = tsi_set_min_and_max_tls_versions(impl->ssl_contexts[i],
                                                options->min_tls_version,
                                                options->max_tls_version);
      if (result != TSI_OK) return result;

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
            strlen(options->pem_client_root_certs),
            options->send_client_ca_list ? &root_names : nullptr);
        if (result != TSI_OK) {
          gpr_log(GPR_ERROR, "Invalid verification certs.");
          break;
        }
        if (options->send_client_ca_list) {
          SSL_CTX_set_client_CA_list(impl->ssl_contexts[i], root_names);
        }
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
          SSL_CTX_set_verify(impl->ssl_contexts[i], SSL_VERIFY_PEER,
                             RootCertExtractCallback);
          break;
        case TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_BUT_DONT_VERIFY:
          SSL_CTX_set_verify(impl->ssl_contexts[i],
                             SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                             NullVerifyCallback);
          break;
        case TSI_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY:
          SSL_CTX_set_verify(impl->ssl_contexts[i],
                             SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                             RootCertExtractCallback);
          break;
      }

#if OPENSSL_VERSION_NUMBER >= 0x10100000
      if (options->crl_directory != nullptr &&
          strcmp(options->crl_directory, "") != 0) {
        gpr_log(GPR_INFO, "enabling server CRL checking with path %s",
                options->crl_directory);
        X509_STORE* cert_store = SSL_CTX_get_cert_store(impl->ssl_contexts[i]);
        X509_STORE_set_verify_cb(cert_store, verify_cb);
        if (!X509_STORE_load_locations(cert_store, nullptr,
                                       options->crl_directory)) {
          gpr_log(GPR_ERROR, "Failed to load CRL File from directory.");
        } else {
          X509_VERIFY_PARAM* param = X509_STORE_get0_param(cert_store);
          X509_VERIFY_PARAM_set_flags(
              param, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
          gpr_log(GPR_INFO, "enabled server CRL checking.");
        }
      }
#endif

      result = tsi_ssl_extract_x509_subject_names_from_pem_cert(
          options->pem_key_cert_pairs[i].cert_chain,
          &impl->ssl_context_x509_subject_names[i]);
      if (result != TSI_OK) break;

      SSL_CTX_set_tlsext_servername_callback(
          impl->ssl_contexts[i],
          ssl_server_handshaker_factory_servername_callback);
      SSL_CTX_set_tlsext_servername_arg(impl->ssl_contexts[i], impl);
#if TSI_OPENSSL_ALPN_SUPPORT
      SSL_CTX_set_alpn_select_cb(impl->ssl_contexts[i],
                                 server_handshaker_factory_alpn_callback, impl);
#endif  // TSI_OPENSSL_ALPN_SUPPORT
      SSL_CTX_set_next_protos_advertised_cb(
          impl->ssl_contexts[i],
          server_handshaker_factory_npn_advertised_callback, impl);

#if OPENSSL_VERSION_NUMBER >= 0x10101000 && !defined(LIBRESSL_VERSION_NUMBER)
      // Register factory at index
      if (options->key_logger != nullptr) {
        // Need to set factory at g_ssl_ctx_ex_factory_index
        SSL_CTX_set_ex_data(impl->ssl_contexts[i], g_ssl_ctx_ex_factory_index,
                            impl);
        // SSL_CTX_set_keylog_callback is set here to register callback
        // when ssl/tls handshakes complete.
        SSL_CTX_set_keylog_callback(
            impl->ssl_contexts[i],
            ssl_keylogging_callback<tsi_ssl_server_handshaker_factory>);
      }
#endif
    } while (false);

    if (result != TSI_OK) {
      tsi_ssl_handshaker_factory_unref(&impl->base);
      return result;
    }
  }

  *factory = impl;
  return TSI_OK;
}

// --- tsi_ssl utils. ---

int tsi_ssl_peer_matches_name(const tsi_peer* peer, absl::string_view name) {
  size_t i = 0;
  size_t san_count = 0;
  const tsi_peer_property* cn_property = nullptr;
  int like_ip = looks_like_ip_address(name);

  // Check the SAN first.
  for (i = 0; i < peer->property_count; i++) {
    const tsi_peer_property* property = &peer->properties[i];
    if (property->name == nullptr) continue;
    if (strcmp(property->name,
               TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY) == 0) {
      san_count++;

      absl::string_view entry(property->value.data, property->value.length);
      if (!like_ip && does_entry_match_name(entry, name)) {
        return 1;
      } else if (like_ip && name == entry) {
        // IP Addresses are exact matches only.
        return 1;
      }
    } else if (strcmp(property->name,
                      TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY) == 0) {
      cn_property = property;
    }
  }

  // If there's no SAN, try the CN, but only if its not like an IP Address
  if (san_count == 0 && cn_property != nullptr && !like_ip) {
    if (does_entry_match_name(absl::string_view(cn_property->value.data,
                                                cn_property->value.length),
                              name)) {
      return 1;
    }
  }

  return 0;  // Not found.
}

// --- Testing support. ---
const tsi_ssl_handshaker_factory_vtable* tsi_ssl_handshaker_factory_swap_vtable(
    tsi_ssl_handshaker_factory* factory,
    tsi_ssl_handshaker_factory_vtable* new_vtable) {
  GPR_ASSERT(factory != nullptr);
  GPR_ASSERT(factory->vtable != nullptr);

  const tsi_ssl_handshaker_factory_vtable* orig_vtable = factory->vtable;
  factory->vtable = new_vtable;
  return orig_vtable;
}
