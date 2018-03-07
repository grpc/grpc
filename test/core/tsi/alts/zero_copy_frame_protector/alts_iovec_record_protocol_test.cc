/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h"
#include "test/core/tsi/alts/crypt/gsec_test_util.h"

constexpr size_t kMaxDataSize = 1024;
constexpr size_t kMaxSlices = 10;
constexpr size_t kSealRepeatTimes = 5;
constexpr size_t kTagLength = 16;

/* Test fixtures for each test cases.  */
struct alts_iovec_record_protocol_test_fixture {
  alts_iovec_record_protocol* client_protect;
  alts_iovec_record_protocol* client_unprotect;
  alts_iovec_record_protocol* server_protect;
  alts_iovec_record_protocol* server_unprotect;
};

/* Test variables for protect/unprotect operations.  */
struct alts_iovec_record_protocol_test_var {
  uint8_t* header_buf;
  size_t header_length;
  iovec_t header_iovec;
  uint8_t* tag_buf;
  size_t tag_length;
  iovec_t tag_iovec;
  uint8_t* data_buf;
  uint8_t* dup_buf;
  size_t data_length;
  iovec_t* data_iovec;
  size_t data_iovec_length;
  uint8_t* protected_buf;
  iovec_t protected_iovec;
  iovec_t unprotected_iovec;
};

/* --- Test utility functions. --- */

static void randomly_slice(uint8_t* input, size_t input_length,
                           iovec_t** output, size_t* output_length) {
  if (input_length == 0) {
    *output = nullptr;
    *output_length = 0;
    return;
  }
  *output_length = gsec_test_bias_random_uint32(kMaxSlices) + 1;
  *output = static_cast<iovec_t*>(gpr_malloc(*output_length * sizeof(iovec_t)));
  for (size_t i = 0; i < *output_length - 1; i++) {
    size_t slice_length =
        gsec_test_bias_random_uint32(static_cast<uint32_t>(input_length));
    iovec_t slice = {input, slice_length};
    (*output)[i] = slice;
    input += slice_length;
    input_length -= slice_length;
  }
  iovec_t slice = {input, input_length};
  (*output)[*output_length - 1] = slice;
}

static size_t alter_random_byte(uint8_t* buf, size_t buf_length) {
  GPR_ASSERT(buf != nullptr);
  uint32_t offset =
      gsec_test_bias_random_uint32(static_cast<uint32_t>(buf_length));
  (*(buf + offset))++;
  return offset;
}

static void revert_back_alter(uint8_t* buf, size_t offset) {
  GPR_ASSERT(buf != nullptr);
  (*(buf + offset))--;
}

static alts_iovec_record_protocol_test_fixture*
alts_iovec_record_protocol_test_fixture_create(bool rekey,
                                               bool integrity_only) {
  alts_iovec_record_protocol_test_fixture* fixture =
      static_cast<alts_iovec_record_protocol_test_fixture*>(
          gpr_malloc(sizeof(alts_iovec_record_protocol_test_fixture)));
  size_t overflow_size = 8;
  size_t key_length = rekey ? kAes128GcmRekeyKeyLength : kAes128GcmKeyLength;
  uint8_t* key;
  gsec_test_random_array(&key, key_length);
  gsec_aead_crypter* crypter = nullptr;
  /* Create client record protocol for protect.  */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_iovec_record_protocol_create(
                 crypter, overflow_size, /*is_client=*/true, integrity_only,
                 /*is_protect=*/true, &fixture->client_protect,
                 nullptr) == GRPC_STATUS_OK);
  /* Create client record protocol for unprotect.  */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_iovec_record_protocol_create(
                 crypter, overflow_size, /*is_client=*/true, integrity_only,
                 /*is_protect=*/false, &fixture->client_unprotect,
                 nullptr) == GRPC_STATUS_OK);
  /* Create server record protocol for protect.  */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_iovec_record_protocol_create(
                 crypter, overflow_size, /*is_client=*/false, integrity_only,
                 /*is_protect=*/true, &fixture->server_protect,
                 nullptr) == GRPC_STATUS_OK);
  /* Create server record protocol for unprotect.  */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_iovec_record_protocol_create(
                 crypter, overflow_size, /*is_client=*/false, integrity_only,
                 /*is_protect=*/false, &fixture->server_unprotect,
                 nullptr) == GRPC_STATUS_OK);

  gpr_free(key);
  return fixture;
}

static void alts_iovec_record_protocol_test_fixture_destroy(
    alts_iovec_record_protocol_test_fixture* fixture) {
  if (fixture == nullptr) {
    return;
  }
  alts_iovec_record_protocol_destroy(fixture->client_protect);
  alts_iovec_record_protocol_destroy(fixture->client_unprotect);
  alts_iovec_record_protocol_destroy(fixture->server_protect);
  alts_iovec_record_protocol_destroy(fixture->server_unprotect);
  gpr_free(fixture);
}

static alts_iovec_record_protocol_test_var*
alts_iovec_record_protocol_test_var_create() {
  auto* var = static_cast<alts_iovec_record_protocol_test_var*>(
      gpr_zalloc(sizeof(alts_iovec_record_protocol_test_var)));
  /* Sets header buffer.  */
  var->header_length = alts_iovec_record_protocol_get_header_length();
  var->header_buf = static_cast<uint8_t*>(gpr_malloc(var->header_length));
  var->header_iovec.iov_base = var->header_buf;
  var->header_iovec.iov_len = var->header_length;
  /* Sets tag buffer.  */
  var->tag_length = kTagLength;
  var->tag_buf = static_cast<uint8_t*>(gpr_malloc(var->tag_length));
  var->tag_iovec.iov_base = var->tag_buf;
  var->tag_iovec.iov_len = var->tag_length;
  /* Randomly sets data buffer and duplicates to dup_buf.  */
  var->data_length = gsec_test_bias_random_uint32(kMaxDataSize) + 1;
  var->data_buf = static_cast<uint8_t*>(gpr_malloc(var->data_length));
  gsec_test_random_bytes(var->data_buf, var->data_length);
  gsec_test_copy(var->data_buf, &var->dup_buf, var->data_length);
  var->data_iovec = nullptr;
  var->data_iovec_length = 0;
  randomly_slice(var->data_buf, var->data_length, &var->data_iovec,
                 &var->data_iovec_length);
  /* Sets protected iovec.  */
  size_t protected_buf_length =
      var->header_length + var->data_length + var->tag_length;
  var->protected_buf = static_cast<uint8_t*>(gpr_malloc(protected_buf_length));
  var->protected_iovec.iov_base = var->protected_buf;
  var->protected_iovec.iov_len = protected_buf_length;
  /* Unprotected iovec points to data_buf.  */
  var->unprotected_iovec.iov_base = var->data_buf;
  var->unprotected_iovec.iov_len = var->data_length;
  return var;
}

static void alts_iovec_record_protocol_test_var_destroy(
    alts_iovec_record_protocol_test_var* var) {
  if (var == nullptr) {
    return;
  }
  gpr_free(var->header_buf);
  gpr_free(var->tag_buf);
  gpr_free(var->data_buf);
  gpr_free(var->dup_buf);
  gpr_free(var->data_iovec);
  gpr_free(var->protected_buf);
  gpr_free(var);
}

/* --- Integrity-only protect/unprotect tests. --- */

static void integrity_only_random_seal_unseal(
    alts_iovec_record_protocol* sender, alts_iovec_record_protocol* receiver) {
  for (size_t i = 0; i < kSealRepeatTimes; i++) {
    alts_iovec_record_protocol_test_var* var =
        alts_iovec_record_protocol_test_var_create();
    /* Seals and then unseals.  */
    grpc_status_code status = alts_iovec_record_protocol_integrity_only_protect(
        sender, var->data_iovec, var->data_iovec_length, var->header_iovec,
        var->tag_iovec, nullptr);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    gpr_free(var->data_iovec);
    /* Randomly slices data buffer again.  */
    randomly_slice(var->data_buf, var->data_length, &var->data_iovec,
                   &var->data_iovec_length);
    status = alts_iovec_record_protocol_integrity_only_unprotect(
        receiver, var->data_iovec, var->data_iovec_length, var->header_iovec,
        var->tag_iovec, nullptr);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    /* Makes sure data buffer has not been modified during
     * seal/unseal.  */
    GPR_ASSERT(memcmp(var->data_buf, var->dup_buf, var->data_length) == 0);
    alts_iovec_record_protocol_test_var_destroy(var);
  }
}

static void integrity_only_empty_seal_unseal(
    alts_iovec_record_protocol* sender, alts_iovec_record_protocol* receiver) {
  for (size_t i = 0; i < kSealRepeatTimes; i++) {
    alts_iovec_record_protocol_test_var* var =
        alts_iovec_record_protocol_test_var_create();
    /* Seals and then unseals empty payload.  */
    grpc_status_code status = alts_iovec_record_protocol_integrity_only_protect(
        sender, nullptr, 0, var->header_iovec, var->tag_iovec, nullptr);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    status = alts_iovec_record_protocol_integrity_only_unprotect(
        receiver, nullptr, 0, var->header_iovec, var->tag_iovec, nullptr);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    alts_iovec_record_protocol_test_var_destroy(var);
  }
}

static void integrity_only_unsync_seal_unseal(
    alts_iovec_record_protocol* sender, alts_iovec_record_protocol* receiver) {
  /* Seals once.  */
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  grpc_status_code status = alts_iovec_record_protocol_integrity_only_protect(
      sender, var->data_iovec, var->data_iovec_length, var->header_iovec,
      var->tag_iovec, nullptr);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  alts_iovec_record_protocol_test_var_destroy(var);
  /* Seals again.  */
  var = alts_iovec_record_protocol_test_var_create();
  status = alts_iovec_record_protocol_integrity_only_protect(
      sender, var->data_iovec, var->data_iovec_length, var->header_iovec,
      var->tag_iovec, nullptr);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  /* Unseals the second frame.  */
  char* error_message = nullptr;
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      receiver, var->data_iovec, var->data_iovec_length, var->header_iovec,
      var->tag_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message,
      "Frame tag verification failed."));
  gpr_free(error_message);
  alts_iovec_record_protocol_test_var_destroy(var);
}

static void integrity_only_corrupted_data(
    alts_iovec_record_protocol* sender, alts_iovec_record_protocol* receiver) {
  /* Seals the data first.  */
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  grpc_status_code status = alts_iovec_record_protocol_integrity_only_protect(
      sender, var->data_iovec, var->data_iovec_length, var->header_iovec,
      var->tag_iovec, nullptr);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  /* Alter frame length field.  */
  char* error_message = nullptr;
  size_t offset =
      alter_random_byte(var->header_buf, kZeroCopyFrameLengthFieldSize);
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      receiver, var->data_iovec, var->data_iovec_length, var->header_iovec,
      var->tag_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message, "Bad frame length."));
  gpr_free(error_message);
  revert_back_alter(var->header_buf, offset);
  /* Alter message type field.  */
  offset = alter_random_byte(var->header_buf + kZeroCopyFrameLengthFieldSize,
                             kZeroCopyFrameMessageTypeFieldSize);
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      receiver, var->data_iovec, var->data_iovec_length, var->header_iovec,
      var->tag_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message,
      "Unsupported message type."));
  gpr_free(error_message);
  revert_back_alter(var->header_buf + kZeroCopyFrameLengthFieldSize, offset);
  /* Alter data.  */
  offset = alter_random_byte(var->data_buf, var->data_length);
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      receiver, var->data_iovec, var->data_iovec_length, var->header_iovec,
      var->tag_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message,
      "Frame tag verification failed."));
  gpr_free(error_message);
  revert_back_alter(var->data_buf, offset);
  /* Alter tag.  */
  offset = alter_random_byte(var->tag_buf, var->tag_length);
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      receiver, var->data_iovec, var->data_iovec_length, var->header_iovec,
      var->tag_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message,
      "Frame tag verification failed."));
  gpr_free(error_message);
  revert_back_alter(var->tag_buf, offset);
  /* Reverted protected data should be verified correctly.  */
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      receiver, var->data_iovec, var->data_iovec_length, var->header_iovec,
      var->tag_iovec, nullptr);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  GPR_ASSERT(memcmp(var->data_buf, var->dup_buf, var->data_length) == 0);
  alts_iovec_record_protocol_test_var_destroy(var);
}

static void integrity_only_protect_input_check(alts_iovec_record_protocol* rp) {
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  char* error_message = nullptr;
  /* Header buffer is nullptr.  */
  iovec_t header_iovec = {nullptr, var->header_length};
  grpc_status_code status = alts_iovec_record_protocol_integrity_only_protect(
      rp, var->data_iovec, var->data_iovec_length, header_iovec, var->tag_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Header is nullptr."));
  gpr_free(error_message);
  /* Header buffer length is 0.  */
  header_iovec.iov_base = var->header_buf;
  header_iovec.iov_len = 0;
  status = alts_iovec_record_protocol_integrity_only_protect(
      rp, var->data_iovec, var->data_iovec_length, header_iovec, var->tag_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Header length is incorrect."));
  gpr_free(error_message);
  /* Tag buffer is nullptr.  */
  iovec_t tag_iovec = {nullptr, var->tag_length};
  status = alts_iovec_record_protocol_integrity_only_protect(
      rp, var->data_iovec, var->data_iovec_length, var->header_iovec, tag_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message, "Tag is nullptr."));
  gpr_free(error_message);
  /* Tag buffer length is 0.  */
  tag_iovec.iov_base = var->tag_buf;
  tag_iovec.iov_len = 0;
  status = alts_iovec_record_protocol_integrity_only_protect(
      rp, var->data_iovec, var->data_iovec_length, var->header_iovec, tag_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Tag length is incorrect."));
  gpr_free(error_message);
  alts_iovec_record_protocol_test_var_destroy(var);
}

static void integrity_only_unprotect_input_check(
    alts_iovec_record_protocol* rp) {
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  char* error_message = nullptr;
  /* Header buffer is nullptr.  */
  iovec_t header_iovec = {nullptr, var->header_length};
  grpc_status_code status = alts_iovec_record_protocol_integrity_only_unprotect(
      rp, var->data_iovec, var->data_iovec_length, header_iovec, var->tag_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Header is nullptr."));
  gpr_free(error_message);
  /* Header buffer length is 0.  */
  header_iovec.iov_base = var->header_buf;
  header_iovec.iov_len = 0;
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      rp, var->data_iovec, var->data_iovec_length, header_iovec, var->tag_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Header length is incorrect."));
  gpr_free(error_message);
  /* Tag buffer is nullptr.  */
  iovec_t tag_iovec = {nullptr, var->tag_length};
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      rp, var->data_iovec, var->data_iovec_length, var->header_iovec, tag_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message, "Tag is nullptr."));
  gpr_free(error_message);
  /* Tag buffer length is 0.  */
  tag_iovec.iov_base = var->tag_buf;
  tag_iovec.iov_len = 0;
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      rp, var->data_iovec, var->data_iovec_length, var->header_iovec, tag_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Tag length is incorrect."));
  gpr_free(error_message);
  alts_iovec_record_protocol_test_var_destroy(var);
}

/* --- Privacy-integrity protect/unprotect tests. --- */

static void privacy_integrity_random_seal_unseal(
    alts_iovec_record_protocol* sender, alts_iovec_record_protocol* receiver) {
  for (size_t i = 0; i < kSealRepeatTimes; i++) {
    alts_iovec_record_protocol_test_var* var =
        alts_iovec_record_protocol_test_var_create();
    /* Seals and then unseals.  */
    grpc_status_code status =
        alts_iovec_record_protocol_privacy_integrity_protect(
            sender, var->data_iovec, var->data_iovec_length,
            var->protected_iovec, nullptr);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    iovec_t header_iovec = {var->protected_buf, var->header_length};
    gpr_free(var->data_iovec);
    /* Randomly slices protected buffer, excluding the header.  */
    randomly_slice(var->protected_buf + var->header_length,
                   var->data_length + var->tag_length, &var->data_iovec,
                   &var->data_iovec_length);
    status = alts_iovec_record_protocol_privacy_integrity_unprotect(
        receiver, header_iovec, var->data_iovec, var->data_iovec_length,
        var->unprotected_iovec, nullptr);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    /* Makes sure unprotected data are the same as the original.  */
    GPR_ASSERT(memcmp(var->data_buf, var->dup_buf, var->data_length) == 0);
    alts_iovec_record_protocol_test_var_destroy(var);
  }
}

static void privacy_integrity_empty_seal_unseal(
    alts_iovec_record_protocol* sender, alts_iovec_record_protocol* receiver) {
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  size_t empty_payload_frame_size = var->header_length + var->tag_length;
  auto* protected_buf =
      static_cast<uint8_t*>(gpr_malloc(empty_payload_frame_size));
  for (size_t i = 0; i < kSealRepeatTimes; i++) {
    iovec_t protected_iovec = {protected_buf, empty_payload_frame_size};
    iovec_t unprotected_iovec = {nullptr, 0};
    iovec_t data_iovec = {protected_buf + var->header_length, var->tag_length};
    /* Seals and then unseals empty payload.  */
    grpc_status_code status =
        alts_iovec_record_protocol_privacy_integrity_protect(
            sender, nullptr, 0, protected_iovec, nullptr);
    GPR_ASSERT(status == GRPC_STATUS_OK);
    iovec_t header_iovec = {protected_buf, var->header_length};
    status = alts_iovec_record_protocol_privacy_integrity_unprotect(
        receiver, header_iovec, &data_iovec, 1, unprotected_iovec, nullptr);
    GPR_ASSERT(status == GRPC_STATUS_OK);
  }
  gpr_free(protected_buf);
  alts_iovec_record_protocol_test_var_destroy(var);
}

static void privacy_integrity_unsync_seal_unseal(
    alts_iovec_record_protocol* sender, alts_iovec_record_protocol* receiver) {
  /* Seals once.  */
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  grpc_status_code status =
      alts_iovec_record_protocol_privacy_integrity_protect(
          sender, var->data_iovec, var->data_iovec_length, var->protected_iovec,
          nullptr);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  alts_iovec_record_protocol_test_var_destroy(var);
  /* Seals again.  */
  var = alts_iovec_record_protocol_test_var_create();
  status = alts_iovec_record_protocol_privacy_integrity_protect(
      sender, var->data_iovec, var->data_iovec_length, var->protected_iovec,
      nullptr);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  /* Unseals the second frame.  */
  char* error_message = nullptr;
  iovec_t header_iovec = {var->protected_buf, var->header_length};
  iovec_t protected_iovec = {var->protected_buf + var->header_length,
                             var->data_length + var->tag_length};
  status = alts_iovec_record_protocol_privacy_integrity_unprotect(
      receiver, header_iovec, &protected_iovec, 1, var->unprotected_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message, "Frame decryption failed."));
  gpr_free(error_message);
  alts_iovec_record_protocol_test_var_destroy(var);
}

static void privacy_integrity_corrupted_data(
    alts_iovec_record_protocol* sender, alts_iovec_record_protocol* receiver) {
  /* Seals the data first.  */
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  grpc_status_code status =
      alts_iovec_record_protocol_privacy_integrity_protect(
          sender, var->data_iovec, var->data_iovec_length, var->protected_iovec,
          nullptr);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  char* error_message = nullptr;
  uint8_t* header_buf = var->protected_buf;
  size_t header_length = var->header_length;
  iovec_t header_iovec = {header_buf, header_length};
  /* The following protected_buf and protected_length excludes header.  */
  uint8_t* protected_buf = var->protected_buf + var->header_length;
  size_t protected_length = var->data_length + var->tag_length;
  iovec_t protected_iovec = {protected_buf, protected_length};
  /* Alter frame length field.  */
  size_t offset = alter_random_byte(header_buf, kZeroCopyFrameLengthFieldSize);
  status = alts_iovec_record_protocol_privacy_integrity_unprotect(
      receiver, header_iovec, &protected_iovec, 1, var->unprotected_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message, "Bad frame length."));
  gpr_free(error_message);
  revert_back_alter(header_buf, offset);
  /* Alter message type field.  */
  offset = alter_random_byte(header_buf + kZeroCopyFrameLengthFieldSize,
                             kZeroCopyFrameMessageTypeFieldSize);
  status = alts_iovec_record_protocol_privacy_integrity_unprotect(
      receiver, header_iovec, &protected_iovec, 1, var->unprotected_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message,
      "Unsupported message type."));
  gpr_free(error_message);
  revert_back_alter(header_buf + kZeroCopyFrameLengthFieldSize, offset);
  /* Alter protected data.  */
  offset = alter_random_byte(protected_buf, protected_length);
  status = alts_iovec_record_protocol_privacy_integrity_unprotect(
      receiver, header_iovec, &protected_iovec, 1, var->unprotected_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message, "Frame decryption failed."));
  gpr_free(error_message);
  revert_back_alter(protected_buf, offset);
  /* Reverted protected data should be verified correctly.  */
  status = alts_iovec_record_protocol_privacy_integrity_unprotect(
      receiver, header_iovec, &protected_iovec, 1, var->unprotected_iovec,
      nullptr);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  GPR_ASSERT(memcmp(var->data_buf, var->dup_buf, var->data_length) == 0);
  alts_iovec_record_protocol_test_var_destroy(var);
}

static void privacy_integrity_protect_input_check(
    alts_iovec_record_protocol* rp) {
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  char* error_message = nullptr;
  /* Protected output buffer is nullptr.  */
  iovec_t protected_iovec = {nullptr, var->protected_iovec.iov_len};
  grpc_status_code status =
      alts_iovec_record_protocol_privacy_integrity_protect(
          rp, var->data_iovec, var->data_iovec_length, protected_iovec,
          &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Protected frame is nullptr."));
  gpr_free(error_message);
  /* Protected output buffer length incorrect.  */
  protected_iovec.iov_base = var->protected_buf;
  protected_iovec.iov_len = var->header_length + var->data_length;
  status = alts_iovec_record_protocol_privacy_integrity_protect(
      rp, var->data_iovec, var->data_iovec_length, protected_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Protected frame size is incorrect."));
  gpr_free(error_message);
  alts_iovec_record_protocol_test_var_destroy(var);
}

static void privacy_integrity_unprotect_input_check(
    alts_iovec_record_protocol* rp) {
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  char* error_message = nullptr;
  /* Header buffer is nullptr.  */
  iovec_t header_iovec = {var->protected_buf, var->header_length};
  iovec_t protected_iovec = {var->protected_buf + var->header_length,
                             var->data_length + var->tag_length};
  header_iovec.iov_base = nullptr;
  grpc_status_code status =
      alts_iovec_record_protocol_privacy_integrity_unprotect(
          rp, header_iovec, &protected_iovec, 1, var->unprotected_iovec,
          &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Header is nullptr."));
  gpr_free(error_message);
  header_iovec.iov_base = var->protected_buf;
  /* Header buffer length is 0.  */
  header_iovec.iov_len = 0;
  status = alts_iovec_record_protocol_privacy_integrity_unprotect(
      rp, header_iovec, &protected_iovec, 1, var->unprotected_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Header length is incorrect."));
  gpr_free(error_message);
  header_iovec.iov_len = var->header_length;
  /* Unprotected output buffer length is incorrect.  */
  iovec_t unprotected_iovec = {var->data_buf, var->data_length - 1};
  status = alts_iovec_record_protocol_privacy_integrity_unprotect(
      rp, header_iovec, &protected_iovec, 1, unprotected_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INVALID_ARGUMENT, error_message,
      "Unprotected data size is incorrect."));
  gpr_free(error_message);
  alts_iovec_record_protocol_test_var_destroy(var);
}

/* --- Integrity-only and privacy-integrity mixed. --- */

static void record_protocol_wrong_mode(
    alts_iovec_record_protocol* integrity_only_protect_rp,
    alts_iovec_record_protocol* integrity_only_unprotect_rp,
    alts_iovec_record_protocol* privacy_integrity_protect_rp,
    alts_iovec_record_protocol* privacy_integrity_unprotect_rp) {
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  grpc_status_code status;
  char* error_message = nullptr;
  /* Call integrity-only protect on privacy-integrity record protocol.  */
  status = alts_iovec_record_protocol_integrity_only_protect(
      privacy_integrity_protect_rp, var->data_iovec, var->data_iovec_length,
      var->header_iovec, var->tag_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_FAILED_PRECONDITION, error_message,
      "Integrity-only operations are not allowed for this object."));
  gpr_free(error_message);
  /* Call integrity-only unprotect on privacy-integrity record protocol.  */
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      privacy_integrity_unprotect_rp, var->data_iovec, var->data_iovec_length,
      var->header_iovec, var->tag_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_FAILED_PRECONDITION, error_message,
      "Integrity-only operations are not allowed for this object."));
  gpr_free(error_message);
  /* Call privacy-integrity protect on integrity-only record protocol.  */
  status = alts_iovec_record_protocol_privacy_integrity_protect(
      integrity_only_protect_rp, var->data_iovec, var->data_iovec_length,
      var->protected_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_FAILED_PRECONDITION, error_message,
      "Privacy-integrity operations are not allowed for this object."));
  gpr_free(error_message);
  /* Call privacy-integrity unprotect on integrity-only record protocol.  */
  status = alts_iovec_record_protocol_privacy_integrity_unprotect(
      integrity_only_unprotect_rp, var->header_iovec, var->data_iovec,
      var->data_iovec_length, var->unprotected_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_FAILED_PRECONDITION, error_message,
      "Privacy-integrity operations are not allowed for this object."));
  gpr_free(error_message);
  alts_iovec_record_protocol_test_var_destroy(var);
}

static void integrity_seal_privacy_unseal(
    alts_iovec_record_protocol* integrity_only_sender,
    alts_iovec_record_protocol* privacy_integrity_receiver) {
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  grpc_status_code status;
  char* error_message = nullptr;
  /* Seals with integrity-only protect.  */
  status = alts_iovec_record_protocol_integrity_only_protect(
      integrity_only_sender, var->data_iovec, var->data_iovec_length,
      var->header_iovec, var->tag_iovec, nullptr);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  /* Unseal with privacy-integrity unprotect.  */
  memcpy(var->protected_buf, var->data_buf, var->data_length);
  memcpy(var->protected_buf + var->data_length, var->tag_buf, var->tag_length);
  iovec_t protected_iovec = {var->protected_buf,
                             var->data_length + var->tag_length};
  status = alts_iovec_record_protocol_privacy_integrity_unprotect(
      privacy_integrity_receiver, var->header_iovec, &protected_iovec, 1,
      var->unprotected_iovec, &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message, "Frame decryption failed."));
  gpr_free(error_message);
  alts_iovec_record_protocol_test_var_destroy(var);
}

static void privacy_seal_integrity_unseal(
    alts_iovec_record_protocol* privacy_integrity_sender,
    alts_iovec_record_protocol* integrity_only_receiver) {
  alts_iovec_record_protocol_test_var* var =
      alts_iovec_record_protocol_test_var_create();
  grpc_status_code status;
  char* error_message = nullptr;
  /* Seals with privacy-integrity protect.  */
  status = alts_iovec_record_protocol_privacy_integrity_protect(
      privacy_integrity_sender, var->data_iovec, var->data_iovec_length,
      var->protected_iovec, nullptr);
  GPR_ASSERT(status == GRPC_STATUS_OK);
  /* Unseal with integrity-only unprotect.  */
  iovec_t header_iovec = {var->protected_buf, var->header_length};
  iovec_t data_iovec = {var->protected_buf + var->header_length,
                        var->data_length};
  iovec_t tag_iovec = {
      var->protected_buf + var->header_length + var->data_length,
      var->tag_length};
  status = alts_iovec_record_protocol_integrity_only_unprotect(
      integrity_only_receiver, &data_iovec, 1, header_iovec, tag_iovec,
      &error_message);
  GPR_ASSERT(gsec_test_expect_compare_code_and_substr(
      status, GRPC_STATUS_INTERNAL, error_message,
      "Frame tag verification failed."));
  gpr_free(error_message);
  alts_iovec_record_protocol_test_var_destroy(var);
}

/* --- Test cases. --- */

static void alts_iovec_record_protocol_random_seal_unseal_tests() {
  alts_iovec_record_protocol_test_fixture* fixture =
      alts_iovec_record_protocol_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/true);
  integrity_only_random_seal_unseal(fixture->client_protect,
                                    fixture->server_unprotect);
  integrity_only_random_seal_unseal(fixture->server_protect,
                                    fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/true);
  integrity_only_random_seal_unseal(fixture->client_protect,
                                    fixture->server_unprotect);
  integrity_only_random_seal_unseal(fixture->server_protect,
                                    fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/false, /*integrity_only=*/false);
  privacy_integrity_random_seal_unseal(fixture->client_protect,
                                       fixture->server_unprotect);
  privacy_integrity_random_seal_unseal(fixture->server_protect,
                                       fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/false);
  privacy_integrity_random_seal_unseal(fixture->client_protect,
                                       fixture->server_unprotect);
  privacy_integrity_random_seal_unseal(fixture->server_protect,
                                       fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);
}

static void alts_iovec_record_protocol_empty_seal_unseal_tests() {
  alts_iovec_record_protocol_test_fixture* fixture =
      alts_iovec_record_protocol_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/true);
  integrity_only_empty_seal_unseal(fixture->client_protect,
                                   fixture->server_unprotect);
  integrity_only_empty_seal_unseal(fixture->server_protect,
                                   fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/true);
  integrity_only_empty_seal_unseal(fixture->client_protect,
                                   fixture->server_unprotect);
  integrity_only_empty_seal_unseal(fixture->server_protect,
                                   fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/false, /*integrity_only=*/false);
  privacy_integrity_empty_seal_unseal(fixture->client_protect,
                                      fixture->server_unprotect);
  privacy_integrity_empty_seal_unseal(fixture->server_protect,
                                      fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/false);
  privacy_integrity_empty_seal_unseal(fixture->client_protect,
                                      fixture->server_unprotect);
  privacy_integrity_empty_seal_unseal(fixture->server_protect,
                                      fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);
}

static void alts_iovec_record_protocol_unsync_seal_unseal_tests() {
  alts_iovec_record_protocol_test_fixture* fixture =
      alts_iovec_record_protocol_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/true);
  integrity_only_unsync_seal_unseal(fixture->client_protect,
                                    fixture->server_unprotect);
  integrity_only_unsync_seal_unseal(fixture->server_protect,
                                    fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/true);
  integrity_only_unsync_seal_unseal(fixture->client_protect,
                                    fixture->server_unprotect);
  integrity_only_unsync_seal_unseal(fixture->server_protect,
                                    fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/false, /*integrity_only=*/false);
  privacy_integrity_unsync_seal_unseal(fixture->client_protect,
                                       fixture->server_unprotect);
  privacy_integrity_unsync_seal_unseal(fixture->server_protect,
                                       fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/false);
  privacy_integrity_unsync_seal_unseal(fixture->client_protect,
                                       fixture->server_unprotect);
  privacy_integrity_unsync_seal_unseal(fixture->server_protect,
                                       fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);
}

static void alts_iovec_record_protocol_corrupted_data_tests() {
  alts_iovec_record_protocol_test_fixture* fixture =
      alts_iovec_record_protocol_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/true);
  integrity_only_corrupted_data(fixture->client_protect,
                                fixture->server_unprotect);
  integrity_only_corrupted_data(fixture->server_protect,
                                fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/true);
  integrity_only_corrupted_data(fixture->client_protect,
                                fixture->server_unprotect);
  integrity_only_corrupted_data(fixture->server_protect,
                                fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/false, /*integrity_only=*/false);
  privacy_integrity_corrupted_data(fixture->client_protect,
                                   fixture->server_unprotect);
  privacy_integrity_corrupted_data(fixture->server_protect,
                                   fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/false);
  privacy_integrity_corrupted_data(fixture->client_protect,
                                   fixture->server_unprotect);
  privacy_integrity_corrupted_data(fixture->server_protect,
                                   fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);
}

static void alts_iovec_record_protocol_input_check_tests() {
  alts_iovec_record_protocol_test_fixture* fixture =
      alts_iovec_record_protocol_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/true);
  integrity_only_protect_input_check(fixture->client_protect);
  integrity_only_unprotect_input_check(fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/true);
  integrity_only_protect_input_check(fixture->client_protect);
  integrity_only_unprotect_input_check(fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/false, /*integrity_only=*/false);
  privacy_integrity_protect_input_check(fixture->client_protect);
  privacy_integrity_unprotect_input_check(fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);

  fixture = alts_iovec_record_protocol_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/false);
  privacy_integrity_protect_input_check(fixture->client_protect);
  privacy_integrity_unprotect_input_check(fixture->client_unprotect);
  alts_iovec_record_protocol_test_fixture_destroy(fixture);
}

static void alts_iovec_record_protocol_mix_operations_tests() {
  alts_iovec_record_protocol_test_fixture* fixture_1 =
      alts_iovec_record_protocol_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/true);
  alts_iovec_record_protocol_test_fixture* fixture_2 =
      alts_iovec_record_protocol_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/false);

  record_protocol_wrong_mode(
      fixture_1->client_protect, fixture_1->client_unprotect,
      fixture_2->client_protect, fixture_2->client_unprotect);
  integrity_seal_privacy_unseal(fixture_1->client_protect,
                                fixture_2->server_unprotect);
  privacy_seal_integrity_unseal(fixture_2->client_protect,
                                fixture_1->server_unprotect);

  alts_iovec_record_protocol_test_fixture_destroy(fixture_1);
  alts_iovec_record_protocol_test_fixture_destroy(fixture_2);
}

int main(int argc, char** argv) {
  alts_iovec_record_protocol_random_seal_unseal_tests();
  alts_iovec_record_protocol_empty_seal_unseal_tests();
  alts_iovec_record_protocol_unsync_seal_unseal_tests();
  alts_iovec_record_protocol_corrupted_data_tests();
  alts_iovec_record_protocol_input_check_tests();
  alts_iovec_record_protocol_mix_operations_tests();
  return 0;
}
