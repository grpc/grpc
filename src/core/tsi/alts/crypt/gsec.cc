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

#include "src/core/tsi/alts/crypt/gsec.h"

#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <stdio.h>
#include <string.h>

static const char vtable_error_msg[] =
    "crypter or crypter->vtable has not been initialized properly";

static void maybe_copy_error_msg(const char* src, char** dst) {
  if (dst != nullptr && src != nullptr) {
    *dst = static_cast<char*>(gpr_malloc(strlen(src) + 1));
    memcpy(*dst, src, strlen(src) + 1);
  }
}

grpc_status_code gsec_aead_crypter_encrypt(
    gsec_aead_crypter* crypter, const uint8_t* nonce, size_t nonce_length,
    const uint8_t* aad, size_t aad_length, const uint8_t* plaintext,
    size_t plaintext_length, uint8_t* ciphertext_and_tag,
    size_t ciphertext_and_tag_length, size_t* bytes_written,
    char** error_details) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->encrypt_iovec != nullptr) {
    struct iovec aad_vec = {const_cast<uint8_t*>(aad), aad_length};
    struct iovec plaintext_vec = {const_cast<uint8_t*>(plaintext),
                                  plaintext_length};
    struct iovec ciphertext_vec = {ciphertext_and_tag,
                                   ciphertext_and_tag_length};
    return crypter->vtable->encrypt_iovec(
        crypter, nonce, nonce_length, &aad_vec, 1, &plaintext_vec, 1,
        ciphertext_vec, bytes_written, error_details);
  }
  // An error occurred.
  maybe_copy_error_msg(vtable_error_msg, error_details);
  return GRPC_STATUS_INVALID_ARGUMENT;
}

grpc_status_code gsec_aead_crypter_encrypt_iovec(
    gsec_aead_crypter* crypter, const uint8_t* nonce, size_t nonce_length,
    const struct iovec* aad_vec, size_t aad_vec_length,
    const struct iovec* plaintext_vec, size_t plaintext_vec_length,
    struct iovec ciphertext_vec, size_t* ciphertext_bytes_written,
    char** error_details) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->encrypt_iovec != nullptr) {
    return crypter->vtable->encrypt_iovec(
        crypter, nonce, nonce_length, aad_vec, aad_vec_length, plaintext_vec,
        plaintext_vec_length, ciphertext_vec, ciphertext_bytes_written,
        error_details);
  }
  // An error occurred.
  maybe_copy_error_msg(vtable_error_msg, error_details);
  return GRPC_STATUS_INVALID_ARGUMENT;
}

grpc_status_code gsec_aead_crypter_decrypt(
    gsec_aead_crypter* crypter, const uint8_t* nonce, size_t nonce_length,
    const uint8_t* aad, size_t aad_length, const uint8_t* ciphertext_and_tag,
    size_t ciphertext_and_tag_length, uint8_t* plaintext,
    size_t plaintext_length, size_t* bytes_written, char** error_details) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->decrypt_iovec != nullptr) {
    struct iovec aad_vec = {const_cast<uint8_t*>(aad), aad_length};
    struct iovec ciphertext_vec = {const_cast<uint8_t*>(ciphertext_and_tag),
                                   ciphertext_and_tag_length};
    struct iovec plaintext_vec = {plaintext, plaintext_length};
    return crypter->vtable->decrypt_iovec(
        crypter, nonce, nonce_length, &aad_vec, 1, &ciphertext_vec, 1,
        plaintext_vec, bytes_written, error_details);
  }
  // An error occurred.
  maybe_copy_error_msg(vtable_error_msg, error_details);
  return GRPC_STATUS_INVALID_ARGUMENT;
}

grpc_status_code gsec_aead_crypter_decrypt_iovec(
    gsec_aead_crypter* crypter, const uint8_t* nonce, size_t nonce_length,
    const struct iovec* aad_vec, size_t aad_vec_length,
    const struct iovec* ciphertext_vec, size_t ciphertext_vec_length,
    struct iovec plaintext_vec, size_t* plaintext_bytes_written,
    char** error_details) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->encrypt_iovec != nullptr) {
    return crypter->vtable->decrypt_iovec(
        crypter, nonce, nonce_length, aad_vec, aad_vec_length, ciphertext_vec,
        ciphertext_vec_length, plaintext_vec, plaintext_bytes_written,
        error_details);
  }
  // An error occurred.
  maybe_copy_error_msg(vtable_error_msg, error_details);
  return GRPC_STATUS_INVALID_ARGUMENT;
}

grpc_status_code gsec_aead_crypter_max_ciphertext_and_tag_length(
    const gsec_aead_crypter* crypter, size_t plaintext_length,
    size_t* max_ciphertext_and_tag_length_to_return, char** error_details) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->max_ciphertext_and_tag_length != nullptr) {
    return crypter->vtable->max_ciphertext_and_tag_length(
        crypter, plaintext_length, max_ciphertext_and_tag_length_to_return,
        error_details);
  }
  // An error occurred.
  maybe_copy_error_msg(vtable_error_msg, error_details);
  return GRPC_STATUS_INVALID_ARGUMENT;
}

grpc_status_code gsec_aead_crypter_max_plaintext_length(
    const gsec_aead_crypter* crypter, size_t ciphertext_and_tag_length,
    size_t* max_plaintext_length_to_return, char** error_details) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->max_plaintext_length != nullptr) {
    return crypter->vtable->max_plaintext_length(
        crypter, ciphertext_and_tag_length, max_plaintext_length_to_return,
        error_details);
  }
  // An error occurred.
  maybe_copy_error_msg(vtable_error_msg, error_details);
  return GRPC_STATUS_INVALID_ARGUMENT;
}

grpc_status_code gsec_aead_crypter_nonce_length(
    const gsec_aead_crypter* crypter, size_t* nonce_length_to_return,
    char** error_details) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->nonce_length != nullptr) {
    return crypter->vtable->nonce_length(crypter, nonce_length_to_return,
                                         error_details);
  }
  // An error occurred.
  maybe_copy_error_msg(vtable_error_msg, error_details);
  return GRPC_STATUS_INVALID_ARGUMENT;
}

grpc_status_code gsec_aead_crypter_key_length(const gsec_aead_crypter* crypter,
                                              size_t* key_length_to_return,
                                              char** error_details) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->key_length != nullptr) {
    return crypter->vtable->key_length(crypter, key_length_to_return,
                                       error_details);
  }
  // An error occurred
  maybe_copy_error_msg(vtable_error_msg, error_details);
  return GRPC_STATUS_INVALID_ARGUMENT;
}

grpc_status_code gsec_aead_crypter_tag_length(const gsec_aead_crypter* crypter,
                                              size_t* tag_length_to_return,
                                              char** error_details) {
  if (crypter != nullptr && crypter->vtable != nullptr &&
      crypter->vtable->tag_length != nullptr) {
    return crypter->vtable->tag_length(crypter, tag_length_to_return,
                                       error_details);
  }
  // An error occurred.
  maybe_copy_error_msg(vtable_error_msg, error_details);
  return GRPC_STATUS_INVALID_ARGUMENT;
}

void gsec_aead_crypter_destroy(gsec_aead_crypter* crypter) {
  if (crypter != nullptr) {
    if (crypter->vtable != nullptr && crypter->vtable->destruct != nullptr) {
      crypter->vtable->destruct(crypter);
    }
    gpr_free(crypter);
  }
}
