//
//
// Copyright 2018 gRPC authors.
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

#include <string.h>

#include <memory>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "absl/types/span.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>

#include "src/core/tsi/alts/crypt/gsec.h"

constexpr size_t kKdfKeyLen = 32;
constexpr size_t kKdfCounterLen = 6;
constexpr size_t kKdfCounterOffset = 2;
constexpr size_t kRekeyAeadKeyLen = kAes128GcmKeyLength;

namespace grpc_core {

GsecKeyFactory::GsecKeyFactory(absl::Span<const uint8_t> key, bool is_rekey)
    : key_(key.begin(), key.end()), is_rekey_(is_rekey) {}

std::unique_ptr<GsecKeyInterface> GsecKeyFactory::Create() const {
  return std::make_unique<GsecKey>(key_, is_rekey_);
}

GsecKey::GsecKey(absl::Span<const uint8_t> key, bool is_rekey)
    : is_rekey_(is_rekey) {
  if (is_rekey_) {
    aead_key_.resize(kRekeyAeadKeyLen);
    kdf_buffer_.resize(EVP_MAX_MD_SIZE);
    nonce_mask_.resize(kAesGcmNonceLength);
    memcpy(nonce_mask_.data(), key.data() + kKdfKeyLen, kAesGcmNonceLength);
    kdf_counter_.resize(kKdfCounterLen, 0);
  }
  key_.resize(is_rekey_ ? kKdfKeyLen : key.size());
  memcpy(key_.data(), key.data(), key_.size());
}

bool GsecKey::IsRekey() { return is_rekey_; }

absl::Span<const uint8_t> GsecKey::key() { return key_; }

absl::Span<const uint8_t> GsecKey::nonce_mask() { return nonce_mask_; }

absl::Span<uint8_t> GsecKey::kdf_counter() {
  return absl::MakeSpan(kdf_counter_);
}

absl::Span<uint8_t> GsecKey::aead_key() { return absl::MakeSpan(aead_key_); }

absl::Span<uint8_t> GsecKey::kdf_buffer() {
  return absl::MakeSpan(kdf_buffer_);
}

}  // namespace grpc_core

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
const char kEvpMacAlgorithm[] = "HMAC";
char kEvpDigest[] = "SHA-256";
#endif

static grpc_status_code aes_gcm_derive_aead_key(
    absl::Span<uint8_t> dst, uint8_t* buf, absl::Span<const uint8_t> kdf_key,
    absl::Span<const uint8_t> kdf_counter) {
  unsigned char ctr = 1;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  HMAC_CTX hmac;
  HMAC_CTX_init(&hmac);
  if (!HMAC_Init_ex(&hmac, kdf_key.data(), kdf_key.size(), EVP_sha256(),
                    nullptr) ||
      !HMAC_Update(&hmac, kdf_counter.data(), kdf_counter.size()) ||
      !HMAC_Update(&hmac, &ctr, 1) || !HMAC_Final(&hmac, buf, nullptr)) {
    HMAC_CTX_cleanup(&hmac);
    return GRPC_STATUS_INTERNAL;
  }
  HMAC_CTX_cleanup(&hmac);
#elif OPENSSL_VERSION_NUMBER < 0x30000000L
  HMAC_CTX* hmac = HMAC_CTX_new();
  if (hmac == nullptr) {
    return GRPC_STATUS_INTERNAL;
  }
  if (!HMAC_Init_ex(hmac, kdf_key.data(), kdf_key.size(), EVP_sha256(),
                    nullptr) ||
      !HMAC_Update(hmac, kdf_counter.data(), kdf_counter.size()) ||
      !HMAC_Update(hmac, &ctr, 1) || !HMAC_Final(hmac, buf, nullptr)) {
    HMAC_CTX_free(hmac);
    return GRPC_STATUS_INTERNAL;
  }
  HMAC_CTX_free(hmac);
#else
  EVP_MAC* mac = EVP_MAC_fetch(nullptr, kEvpMacAlgorithm, nullptr);
  EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
  if (ctx == nullptr) {
    return GRPC_STATUS_INTERNAL;
  }
  OSSL_PARAM params[2];
  params[0] = OSSL_PARAM_construct_utf8_string("digest", kEvpDigest, 0);
  params[1] = OSSL_PARAM_construct_end();

  if (!EVP_MAC_init(ctx, kdf_key.data(), kdf_key.size(), params) ||
      !EVP_MAC_update(ctx, kdf_counter.data(), kdf_counter.size()) ||
      !EVP_MAC_update(ctx, &ctr, 1) ||
      !EVP_MAC_final(ctx, buf, nullptr, EVP_MAX_MD_SIZE)) {
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return GRPC_STATUS_INTERNAL;
  }
  EVP_MAC_CTX_free(ctx);
  EVP_MAC_free(mac);
#endif
  memcpy(dst.data(), buf, dst.size());
  return GRPC_STATUS_OK;
}

// Main struct for AES_GCM crypter interface.
struct gsec_aes_gcm_aead_crypter {
  gsec_aead_crypter crypter;
  size_t nonce_length;
  size_t tag_length;
  EVP_CIPHER_CTX* ctx;
  grpc_core::GsecKeyInterface* gsec_key;
};

static char* aes_gcm_get_openssl_errors() {
  BIO* bio = BIO_new(BIO_s_mem());
  ERR_print_errors(bio);
  BUF_MEM* mem = nullptr;
  char* error_msg = nullptr;
  BIO_get_mem_ptr(bio, &mem);
  if (mem != nullptr) {
    error_msg = static_cast<char*>(gpr_malloc(mem->length + 1));
    memcpy(error_msg, mem->data, mem->length);
    error_msg[mem->length] = '\0';
  }
  BIO_free_all(bio);
  return error_msg;
}

static void aes_gcm_format_errors(const char* error_msg, char** error_details) {
  if (error_details == nullptr) {
    return;
  }
  unsigned long error = ERR_get_error();
  if (error == 0 && error_msg != nullptr) {
    *error_details = static_cast<char*>(gpr_malloc(strlen(error_msg) + 1));
    memcpy(*error_details, error_msg, strlen(error_msg) + 1);
    return;
  }
  char* openssl_errors = aes_gcm_get_openssl_errors();
  if (openssl_errors != nullptr && error_msg != nullptr) {
    size_t len = strlen(error_msg) + strlen(openssl_errors) + 2;  // ", "
    *error_details = static_cast<char*>(gpr_malloc(len + 1));
    snprintf(*error_details, len + 1, "%s, %s", error_msg, openssl_errors);
    gpr_free(openssl_errors);
  }
}

static grpc_status_code gsec_aes_gcm_aead_crypter_max_ciphertext_and_tag_length(
    const gsec_aead_crypter* crypter, size_t plaintext_length,
    size_t* max_ciphertext_and_tag_length, char** error_details) {
  if (max_ciphertext_and_tag_length == nullptr) {
    aes_gcm_format_errors("max_ciphertext_and_tag_length is nullptr.",
                          error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  gsec_aes_gcm_aead_crypter* aes_gcm_crypter =
      reinterpret_cast<gsec_aes_gcm_aead_crypter*>(
          const_cast<gsec_aead_crypter*>(crypter));
  *max_ciphertext_and_tag_length =
      plaintext_length + aes_gcm_crypter->tag_length;
  return GRPC_STATUS_OK;
}

static grpc_status_code gsec_aes_gcm_aead_crypter_max_plaintext_length(
    const gsec_aead_crypter* crypter, size_t ciphertext_and_tag_length,
    size_t* max_plaintext_length, char** error_details) {
  if (max_plaintext_length == nullptr) {
    aes_gcm_format_errors("max_plaintext_length is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  gsec_aes_gcm_aead_crypter* aes_gcm_crypter =
      reinterpret_cast<gsec_aes_gcm_aead_crypter*>(
          const_cast<gsec_aead_crypter*>(crypter));
  if (ciphertext_and_tag_length < aes_gcm_crypter->tag_length) {
    *max_plaintext_length = 0;
    aes_gcm_format_errors(
        "ciphertext_and_tag_length is smaller than tag_length.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  *max_plaintext_length =
      ciphertext_and_tag_length - aes_gcm_crypter->tag_length;
  return GRPC_STATUS_OK;
}

static grpc_status_code gsec_aes_gcm_aead_crypter_nonce_length(
    const gsec_aead_crypter* crypter, size_t* nonce_length,
    char** error_details) {
  if (nonce_length == nullptr) {
    aes_gcm_format_errors("nonce_length is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  gsec_aes_gcm_aead_crypter* aes_gcm_crypter =
      reinterpret_cast<gsec_aes_gcm_aead_crypter*>(
          const_cast<gsec_aead_crypter*>(crypter));
  *nonce_length = aes_gcm_crypter->nonce_length;
  return GRPC_STATUS_OK;
}

static grpc_status_code gsec_aes_gcm_aead_crypter_key_length(
    const gsec_aead_crypter* crypter, size_t* key_length,
    char** error_details) {
  if (key_length == nullptr) {
    aes_gcm_format_errors("key_length is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  gsec_aes_gcm_aead_crypter* aes_gcm_crypter =
      reinterpret_cast<gsec_aes_gcm_aead_crypter*>(
          const_cast<gsec_aead_crypter*>(crypter));
  *key_length = aes_gcm_crypter->gsec_key->key().size();
  return GRPC_STATUS_OK;
}

static grpc_status_code gsec_aes_gcm_aead_crypter_tag_length(
    const gsec_aead_crypter* crypter, size_t* tag_length,
    char** error_details) {
  if (tag_length == nullptr) {
    aes_gcm_format_errors("tag_length is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  gsec_aes_gcm_aead_crypter* aes_gcm_crypter =
      reinterpret_cast<gsec_aes_gcm_aead_crypter*>(
          const_cast<gsec_aead_crypter*>(crypter));
  *tag_length = aes_gcm_crypter->tag_length;
  return GRPC_STATUS_OK;
}

static void aes_gcm_mask_nonce(uint8_t* dst, const uint8_t* nonce,
                               const uint8_t* mask) {
  uint64_t mask1;
  uint32_t mask2;
  memcpy(&mask1, mask, sizeof(mask1));
  memcpy(&mask2, mask + sizeof(mask1), sizeof(mask2));
  uint64_t nonce1;
  uint32_t nonce2;
  memcpy(&nonce1, nonce, sizeof(nonce1));
  memcpy(&nonce2, nonce + sizeof(nonce1), sizeof(nonce2));
  nonce1 ^= mask1;
  nonce2 ^= mask2;
  memcpy(dst, &nonce1, sizeof(nonce1));
  memcpy(dst + sizeof(nonce1), &nonce2, sizeof(nonce2));
}

static grpc_status_code aes_gcm_rekey_if_required(
    gsec_aes_gcm_aead_crypter* aes_gcm_crypter, const uint8_t* nonce,
    char** error_details) {
  // If rekey_data is nullptr, then rekeying is not supported and not required.
  // If bytes 2-7 of kdf_counter differ from the (per message) nonce, then the
  // encryption key is recomputed from a new kdf_counter to ensure that we don't
  // encrypt more than 2^16 messages per encryption key (in each direction).
  if (!aes_gcm_crypter->gsec_key->IsRekey() ||
      memcmp(aes_gcm_crypter->gsec_key->kdf_counter().data(),
             nonce + kKdfCounterOffset,
             aes_gcm_crypter->gsec_key->kdf_counter().size()) == 0) {
    return GRPC_STATUS_OK;
  }
  memcpy(aes_gcm_crypter->gsec_key->kdf_counter().data(),
         nonce + kKdfCounterOffset,
         aes_gcm_crypter->gsec_key->kdf_counter().size());
  if (aes_gcm_derive_aead_key(aes_gcm_crypter->gsec_key->aead_key(),
                              aes_gcm_crypter->gsec_key->kdf_buffer().data(),
                              aes_gcm_crypter->gsec_key->key(),
                              aes_gcm_crypter->gsec_key->kdf_counter()) !=
      GRPC_STATUS_OK) {
    aes_gcm_format_errors("Rekeying failed in key derivation.", error_details);
    return GRPC_STATUS_INTERNAL;
  }
  if (!EVP_DecryptInit_ex(aes_gcm_crypter->ctx, nullptr, nullptr,
                          aes_gcm_crypter->gsec_key->aead_key().data(),
                          nullptr)) {
    aes_gcm_format_errors("Rekeying failed in context update.", error_details);
    return GRPC_STATUS_INTERNAL;
  }
  return GRPC_STATUS_OK;
}

static grpc_status_code gsec_aes_gcm_aead_crypter_encrypt_iovec(
    gsec_aead_crypter* crypter, const uint8_t* nonce, size_t nonce_length,
    const struct iovec* aad_vec, size_t aad_vec_length,
    const struct iovec* plaintext_vec, size_t plaintext_vec_length,
    struct iovec ciphertext_vec, size_t* ciphertext_bytes_written,
    char** error_details) {
  gsec_aes_gcm_aead_crypter* aes_gcm_crypter =
      reinterpret_cast<gsec_aes_gcm_aead_crypter*>(crypter);
  // Input checks
  if (nonce == nullptr) {
    aes_gcm_format_errors("Nonce buffer is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (kAesGcmNonceLength != nonce_length) {
    aes_gcm_format_errors("Nonce buffer has the wrong length.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (aad_vec_length > 0 && aad_vec == nullptr) {
    aes_gcm_format_errors("Non-zero aad_vec_length but aad_vec is nullptr.",
                          error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (plaintext_vec_length > 0 && plaintext_vec == nullptr) {
    aes_gcm_format_errors(
        "Non-zero plaintext_vec_length but plaintext_vec is nullptr.",
        error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (ciphertext_bytes_written == nullptr) {
    aes_gcm_format_errors("bytes_written is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  *ciphertext_bytes_written = 0;
  // rekey if required
  if (aes_gcm_rekey_if_required(aes_gcm_crypter, nonce, error_details) !=
      GRPC_STATUS_OK) {
    return GRPC_STATUS_INTERNAL;
  }
  // mask nonce if required
  const uint8_t* nonce_aead = nonce;
  uint8_t nonce_masked[kAesGcmNonceLength];
  if (aes_gcm_crypter->gsec_key->IsRekey()) {
    aes_gcm_mask_nonce(nonce_masked,
                       aes_gcm_crypter->gsec_key->nonce_mask().data(), nonce);
    nonce_aead = nonce_masked;
  }
  // init openssl context
  if (!EVP_EncryptInit_ex(aes_gcm_crypter->ctx, nullptr, nullptr, nullptr,
                          nonce_aead)) {
    aes_gcm_format_errors("Initializing nonce failed", error_details);
    return GRPC_STATUS_INTERNAL;
  }
  // process aad
  size_t i;
  for (i = 0; i < aad_vec_length; i++) {
    const uint8_t* aad = static_cast<uint8_t*>(aad_vec[i].iov_base);
    size_t aad_length = aad_vec[i].iov_len;
    if (aad_length == 0) {
      continue;
    }
    size_t aad_bytes_read = 0;
    if (aad == nullptr) {
      aes_gcm_format_errors("aad is nullptr.", error_details);
      return GRPC_STATUS_INVALID_ARGUMENT;
    }
    if (!EVP_EncryptUpdate(aes_gcm_crypter->ctx, nullptr,
                           reinterpret_cast<int*>(&aad_bytes_read), aad,
                           static_cast<int>(aad_length)) ||
        aad_bytes_read != aad_length) {
      aes_gcm_format_errors("Setting authenticated associated data failed",
                            error_details);
      return GRPC_STATUS_INTERNAL;
    }
  }
  uint8_t* ciphertext = static_cast<uint8_t*>(ciphertext_vec.iov_base);
  size_t ciphertext_length = ciphertext_vec.iov_len;
  if (ciphertext == nullptr) {
    aes_gcm_format_errors("ciphertext is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  // process plaintext
  for (i = 0; i < plaintext_vec_length; i++) {
    const uint8_t* plaintext = static_cast<uint8_t*>(plaintext_vec[i].iov_base);
    size_t plaintext_length = plaintext_vec[i].iov_len;
    if (plaintext == nullptr) {
      if (plaintext_length == 0) {
        continue;
      }
      aes_gcm_format_errors("plaintext is nullptr.", error_details);
      return GRPC_STATUS_INVALID_ARGUMENT;
    }
    if (ciphertext_length < plaintext_length) {
      aes_gcm_format_errors(
          "ciphertext is not large enough to hold the result.", error_details);
      return GRPC_STATUS_INVALID_ARGUMENT;
    }
    int bytes_written = 0;
    int bytes_to_write = static_cast<int>(plaintext_length);
    if (!EVP_EncryptUpdate(aes_gcm_crypter->ctx, ciphertext, &bytes_written,
                           plaintext, bytes_to_write)) {
      aes_gcm_format_errors("Encrypting plaintext failed.", error_details);
      return GRPC_STATUS_INTERNAL;
    }
    if (bytes_written > bytes_to_write) {
      aes_gcm_format_errors("More bytes written than expected.", error_details);
      return GRPC_STATUS_INTERNAL;
    }
    ciphertext += bytes_written;
    ciphertext_length -= bytes_written;
  }
  int bytes_written_temp = 0;
  if (!EVP_EncryptFinal_ex(aes_gcm_crypter->ctx, nullptr,
                           &bytes_written_temp)) {
    aes_gcm_format_errors("Finalizing encryption failed.", error_details);
    return GRPC_STATUS_INTERNAL;
  }
  if (bytes_written_temp != 0) {
    aes_gcm_format_errors("Openssl wrote some unexpected bytes.",
                          error_details);
    return GRPC_STATUS_INTERNAL;
  }
  if (ciphertext_length < kAesGcmTagLength) {
    aes_gcm_format_errors("ciphertext is too small to hold a tag.",
                          error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }

  if (!EVP_CIPHER_CTX_ctrl(aes_gcm_crypter->ctx, EVP_CTRL_GCM_GET_TAG,
                           kAesGcmTagLength, ciphertext)) {
    aes_gcm_format_errors("Writing tag failed.", error_details);
    return GRPC_STATUS_INTERNAL;
  }
  ciphertext += kAesGcmTagLength;
  ciphertext_length -= kAesGcmTagLength;
  *ciphertext_bytes_written = ciphertext_vec.iov_len - ciphertext_length;
  return GRPC_STATUS_OK;
}

static grpc_status_code gsec_aes_gcm_aead_crypter_decrypt_iovec(
    gsec_aead_crypter* crypter, const uint8_t* nonce, size_t nonce_length,
    const struct iovec* aad_vec, size_t aad_vec_length,
    const struct iovec* ciphertext_vec, size_t ciphertext_vec_length,
    struct iovec plaintext_vec, size_t* plaintext_bytes_written,
    char** error_details) {
  gsec_aes_gcm_aead_crypter* aes_gcm_crypter =
      reinterpret_cast<gsec_aes_gcm_aead_crypter*>(
          const_cast<gsec_aead_crypter*>(crypter));
  if (nonce == nullptr) {
    aes_gcm_format_errors("Nonce buffer is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (kAesGcmNonceLength != nonce_length) {
    aes_gcm_format_errors("Nonce buffer has the wrong length.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (aad_vec_length > 0 && aad_vec == nullptr) {
    aes_gcm_format_errors("Non-zero aad_vec_length but aad_vec is nullptr.",
                          error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (ciphertext_vec_length > 0 && ciphertext_vec == nullptr) {
    aes_gcm_format_errors(
        "Non-zero plaintext_vec_length but plaintext_vec is nullptr.",
        error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  // Compute the total length so we can ensure we don't pass the tag into
  // EVP_decrypt.
  size_t total_ciphertext_length = 0;
  size_t i;
  for (i = 0; i < ciphertext_vec_length; i++) {
    total_ciphertext_length += ciphertext_vec[i].iov_len;
  }
  if (total_ciphertext_length < kAesGcmTagLength) {
    aes_gcm_format_errors("ciphertext is too small to hold a tag.",
                          error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  if (plaintext_bytes_written == nullptr) {
    aes_gcm_format_errors("bytes_written is nullptr.", error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  *plaintext_bytes_written = 0;
  // rekey if required
  if (aes_gcm_rekey_if_required(aes_gcm_crypter, nonce, error_details) !=
      GRPC_STATUS_OK) {
    aes_gcm_format_errors("Rekeying failed.", error_details);
    return GRPC_STATUS_INTERNAL;
  }
  // mask nonce if required
  const uint8_t* nonce_aead = nonce;
  uint8_t nonce_masked[kAesGcmNonceLength];
  if (aes_gcm_crypter->gsec_key->IsRekey()) {
    aes_gcm_mask_nonce(nonce_masked,
                       aes_gcm_crypter->gsec_key->nonce_mask().data(), nonce);
    nonce_aead = nonce_masked;
  }
  // init openssl context
  if (!EVP_DecryptInit_ex(aes_gcm_crypter->ctx, nullptr, nullptr, nullptr,
                          nonce_aead)) {
    aes_gcm_format_errors("Initializing nonce failed.", error_details);
    return GRPC_STATUS_INTERNAL;
  }
  // process aad
  for (i = 0; i < aad_vec_length; i++) {
    const uint8_t* aad = static_cast<uint8_t*>(aad_vec[i].iov_base);
    size_t aad_length = aad_vec[i].iov_len;
    if (aad_length == 0) {
      continue;
    }
    size_t aad_bytes_read = 0;
    if (aad == nullptr) {
      aes_gcm_format_errors("aad is nullptr.", error_details);
      return GRPC_STATUS_INVALID_ARGUMENT;
    }
    if (!EVP_DecryptUpdate(aes_gcm_crypter->ctx, nullptr,
                           reinterpret_cast<int*>(&aad_bytes_read), aad,
                           static_cast<int>(aad_length)) ||
        aad_bytes_read != aad_length) {
      aes_gcm_format_errors("Setting authenticated associated data failed.",
                            error_details);
      return GRPC_STATUS_INTERNAL;
    }
  }
  // process ciphertext
  uint8_t* plaintext = static_cast<uint8_t*>(plaintext_vec.iov_base);
  size_t plaintext_length = plaintext_vec.iov_len;
  if (plaintext_length > 0 && plaintext == nullptr) {
    aes_gcm_format_errors(
        "plaintext is nullptr, but plaintext_length is positive.",
        error_details);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  const uint8_t* ciphertext = nullptr;
  size_t ciphertext_length = 0;
  for (i = 0;
       i < ciphertext_vec_length && total_ciphertext_length > kAesGcmTagLength;
       i++) {
    ciphertext = static_cast<uint8_t*>(ciphertext_vec[i].iov_base);
    ciphertext_length = ciphertext_vec[i].iov_len;
    if (ciphertext == nullptr) {
      if (ciphertext_length == 0) {
        continue;
      }
      aes_gcm_format_errors("ciphertext is nullptr.", error_details);
      memset(plaintext_vec.iov_base, 0x00, plaintext_vec.iov_len);
      return GRPC_STATUS_INVALID_ARGUMENT;
    }
    size_t bytes_written = 0;
    size_t bytes_to_write = ciphertext_length;
    // Don't include the tag
    if (bytes_to_write > total_ciphertext_length - kAesGcmTagLength) {
      bytes_to_write = total_ciphertext_length - kAesGcmTagLength;
    }
    if (plaintext_length < bytes_to_write) {
      aes_gcm_format_errors(
          "Not enough plaintext buffer to hold encrypted ciphertext.",
          error_details);
      return GRPC_STATUS_INVALID_ARGUMENT;
    }
    if (!EVP_DecryptUpdate(aes_gcm_crypter->ctx, plaintext,
                           reinterpret_cast<int*>(&bytes_written), ciphertext,
                           static_cast<int>(bytes_to_write))) {
      aes_gcm_format_errors("Decrypting ciphertext failed.", error_details);
      memset(plaintext_vec.iov_base, 0x00, plaintext_vec.iov_len);
      return GRPC_STATUS_INTERNAL;
    }
    if (bytes_written > ciphertext_length) {
      aes_gcm_format_errors("More bytes written than expected.", error_details);
      memset(plaintext_vec.iov_base, 0x00, plaintext_vec.iov_len);
      return GRPC_STATUS_INTERNAL;
    }
    ciphertext += bytes_written;
    ciphertext_length -= bytes_written;
    total_ciphertext_length -= bytes_written;
    plaintext += bytes_written;
    plaintext_length -= bytes_written;
  }
  if (total_ciphertext_length > kAesGcmTagLength) {
    aes_gcm_format_errors(
        "Not enough plaintext buffer to hold encrypted ciphertext.",
        error_details);
    memset(plaintext_vec.iov_base, 0x00, plaintext_vec.iov_len);
    return GRPC_STATUS_INVALID_ARGUMENT;
  }
  uint8_t tag[kAesGcmTagLength];
  uint8_t* tag_tmp = tag;
  if (ciphertext_length > 0) {
    memcpy(tag_tmp, ciphertext, ciphertext_length);
    tag_tmp += ciphertext_length;
    total_ciphertext_length -= ciphertext_length;
  }
  for (; i < ciphertext_vec_length; i++) {
    ciphertext = static_cast<uint8_t*>(ciphertext_vec[i].iov_base);
    ciphertext_length = ciphertext_vec[i].iov_len;
    if (ciphertext == nullptr) {
      if (ciphertext_length == 0) {
        continue;
      }
      aes_gcm_format_errors("ciphertext is nullptr.", error_details);
      memset(plaintext_vec.iov_base, 0x00, plaintext_vec.iov_len);
      return GRPC_STATUS_INVALID_ARGUMENT;
    }
    memcpy(tag_tmp, ciphertext, ciphertext_length);
    tag_tmp += ciphertext_length;
    total_ciphertext_length -= ciphertext_length;
  }
  if (!EVP_CIPHER_CTX_ctrl(aes_gcm_crypter->ctx, EVP_CTRL_GCM_SET_TAG,
                           kAesGcmTagLength, reinterpret_cast<void*>(tag))) {
    aes_gcm_format_errors("Setting tag failed.", error_details);
    memset(plaintext_vec.iov_base, 0x00, plaintext_vec.iov_len);
    return GRPC_STATUS_INTERNAL;
  }
  int bytes_written_temp = 0;
  if (!EVP_DecryptFinal_ex(aes_gcm_crypter->ctx, nullptr,
                           &bytes_written_temp)) {
    aes_gcm_format_errors("Checking tag failed.", error_details);
    if (plaintext_vec.iov_base != nullptr) {
      memset(plaintext_vec.iov_base, 0x00, plaintext_vec.iov_len);
    }
    return GRPC_STATUS_FAILED_PRECONDITION;
  }
  if (bytes_written_temp != 0) {
    aes_gcm_format_errors("Openssl wrote some unexpected bytes.",
                          error_details);
    memset(plaintext_vec.iov_base, 0x00, plaintext_vec.iov_len);
    return GRPC_STATUS_INTERNAL;
  }
  *plaintext_bytes_written = plaintext_vec.iov_len - plaintext_length;
  return GRPC_STATUS_OK;
}

static void gsec_aes_gcm_aead_crypter_destroy(gsec_aead_crypter* crypter) {
  gsec_aes_gcm_aead_crypter* aes_gcm_crypter =
      reinterpret_cast<gsec_aes_gcm_aead_crypter*>(
          const_cast<gsec_aead_crypter*>(crypter));
  EVP_CIPHER_CTX_free(aes_gcm_crypter->ctx);
  delete aes_gcm_crypter->gsec_key;
}

static const gsec_aead_crypter_vtable vtable = {
    gsec_aes_gcm_aead_crypter_encrypt_iovec,
    gsec_aes_gcm_aead_crypter_decrypt_iovec,
    gsec_aes_gcm_aead_crypter_max_ciphertext_and_tag_length,
    gsec_aes_gcm_aead_crypter_max_plaintext_length,
    gsec_aes_gcm_aead_crypter_nonce_length,
    gsec_aes_gcm_aead_crypter_key_length,
    gsec_aes_gcm_aead_crypter_tag_length,
    gsec_aes_gcm_aead_crypter_destroy};

static grpc_status_code aes_gcm_new_evp_cipher_ctx(
    gsec_aes_gcm_aead_crypter* aes_gcm_crypter, char** error_details) {
  const EVP_CIPHER* cipher = nullptr;
  bool is_rekey = aes_gcm_crypter->gsec_key->IsRekey();
  switch (is_rekey ? kRekeyAeadKeyLen
                   : aes_gcm_crypter->gsec_key->key().size()) {
    case kAes128GcmKeyLength:
      cipher = EVP_aes_128_gcm();
      break;
    case kAes256GcmKeyLength:
      cipher = EVP_aes_256_gcm();
      break;
    default:
      aes_gcm_format_errors("Invalid key length.", error_details);
      return GRPC_STATUS_INTERNAL;
  }
  const uint8_t* aead_key = aes_gcm_crypter->gsec_key->key().data();
  if (is_rekey) {
    if (aes_gcm_derive_aead_key(aes_gcm_crypter->gsec_key->aead_key(),
                                aes_gcm_crypter->gsec_key->kdf_buffer().data(),
                                aes_gcm_crypter->gsec_key->key(),
                                aes_gcm_crypter->gsec_key->kdf_counter()) !=
        GRPC_STATUS_OK) {
      aes_gcm_format_errors("Deriving key failed.", error_details);
      return GRPC_STATUS_INTERNAL;
    }
    aead_key = aes_gcm_crypter->gsec_key->aead_key().data();
  }
  if (!EVP_DecryptInit_ex(aes_gcm_crypter->ctx, cipher, nullptr, aead_key,
                          nullptr)) {
    aes_gcm_format_errors("Setting key failed.", error_details);
    return GRPC_STATUS_INTERNAL;
  }
  if (!EVP_CIPHER_CTX_ctrl(aes_gcm_crypter->ctx, EVP_CTRL_GCM_SET_IVLEN,
                           static_cast<int>(aes_gcm_crypter->nonce_length),
                           nullptr)) {
    aes_gcm_format_errors("Setting nonce length failed.", error_details);
    return GRPC_STATUS_INTERNAL;
  }
  return GRPC_STATUS_OK;
}

grpc_status_code gsec_aes_gcm_aead_crypter_create(
    std::unique_ptr<grpc_core::GsecKeyInterface> key, size_t nonce_length,
    size_t tag_length, gsec_aead_crypter** crypter, char** error_details) {
  if (key == nullptr) {
    aes_gcm_format_errors("key is nullptr.", error_details);
    return GRPC_STATUS_FAILED_PRECONDITION;
  }
  if (crypter == nullptr) {
    aes_gcm_format_errors("crypter is nullptr.", error_details);
    return GRPC_STATUS_FAILED_PRECONDITION;
  }
  *crypter = nullptr;
  if ((key->IsRekey() && key->key().size() != kKdfKeyLen) ||
      (!key->IsRekey() && key->key().size() != kAes128GcmKeyLength &&
       key->key().size() != kAes256GcmKeyLength) ||
      (tag_length != kAesGcmTagLength) ||
      (nonce_length != kAesGcmNonceLength)) {
    aes_gcm_format_errors(
        "Invalid key and/or nonce and/or tag length are provided at AEAD "
        "crypter instance construction time.",
        error_details);
    return GRPC_STATUS_FAILED_PRECONDITION;
  }
  gsec_aes_gcm_aead_crypter* aes_gcm_crypter =
      static_cast<gsec_aes_gcm_aead_crypter*>(
          gpr_malloc(sizeof(gsec_aes_gcm_aead_crypter)));
  aes_gcm_crypter->crypter.vtable = &vtable;
  aes_gcm_crypter->nonce_length = nonce_length;
  aes_gcm_crypter->tag_length = tag_length;
  aes_gcm_crypter->gsec_key = key.release();
  aes_gcm_crypter->ctx = EVP_CIPHER_CTX_new();
  grpc_status_code status =
      aes_gcm_new_evp_cipher_ctx(aes_gcm_crypter, error_details);
  if (status != GRPC_STATUS_OK) {
    gsec_aes_gcm_aead_crypter_destroy(&aes_gcm_crypter->crypter);
    gpr_free(aes_gcm_crypter);
    return status;
  }
  *crypter = &aes_gcm_crypter->crypter;
  return GRPC_STATUS_OK;
}
