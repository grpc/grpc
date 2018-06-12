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

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h"
#include "test/core/tsi/alts/crypt/gsec_test_util.h"

constexpr size_t kMaxSliceLength = 256;
constexpr size_t kMaxSlices = 10;
constexpr size_t kSealRepeatTimes = 5;
constexpr size_t kTagLength = 16;

/* Test fixtures for each test cases.  */
struct alts_grpc_record_protocol_test_fixture {
  alts_grpc_record_protocol* client_protect;
  alts_grpc_record_protocol* client_unprotect;
  alts_grpc_record_protocol* server_protect;
  alts_grpc_record_protocol* server_unprotect;
};

/* Test input variables for protect/unprotect operations.  */
struct alts_grpc_record_protocol_test_var {
  size_t header_length;
  size_t tag_length;
  grpc_slice_buffer original_sb;
  grpc_slice_buffer duplicate_sb;
  grpc_slice_buffer protected_sb;
  grpc_slice_buffer unprotected_sb;
};

/* --- Test utility functions. --- */

static void create_random_slice_buffer(grpc_slice_buffer* sb) {
  GPR_ASSERT(sb != nullptr);
  size_t slice_count = gsec_test_bias_random_uint32(kMaxSlices) + 1;
  for (size_t i = 0; i < slice_count; i++) {
    size_t slice_length = gsec_test_bias_random_uint32(kMaxSliceLength) + 1;
    grpc_slice slice = GRPC_SLICE_MALLOC(slice_length);
    gsec_test_random_bytes(GRPC_SLICE_START_PTR(slice), slice_length);
    grpc_slice_buffer_add(sb, slice);
  }
}

static uint8_t* pointer_to_nth_byte(grpc_slice_buffer* sb, size_t index) {
  GPR_ASSERT(sb != nullptr);
  GPR_ASSERT(index < sb->length);
  for (size_t i = 0; i < sb->count; i++) {
    if (index < GRPC_SLICE_LENGTH(sb->slices[i])) {
      return GRPC_SLICE_START_PTR(sb->slices[i]) + index;
    } else {
      index -= GRPC_SLICE_LENGTH(sb->slices[i]);
    }
  }
  return nullptr;
}

/* Checks if two slice buffer contents are the same. It is not super efficient,
 * but OK for testing.  */
static bool are_slice_buffers_equal(grpc_slice_buffer* first,
                                    grpc_slice_buffer* second) {
  GPR_ASSERT(first != nullptr);
  GPR_ASSERT(second != nullptr);
  if (first->length != second->length) {
    return false;
  }
  for (size_t i = 0; i < first->length; i++) {
    uint8_t* first_ptr = pointer_to_nth_byte(first, i);
    uint8_t* second_ptr = pointer_to_nth_byte(second, i);
    GPR_ASSERT(first_ptr != nullptr);
    GPR_ASSERT(second_ptr != nullptr);
    if ((*first_ptr) != (*second_ptr)) {
      return false;
    }
  }
  return true;
}

static void alter_random_byte(grpc_slice_buffer* sb) {
  GPR_ASSERT(sb != nullptr);
  if (sb->length == 0) {
    return;
  }
  uint32_t offset =
      gsec_test_bias_random_uint32(static_cast<uint32_t>(sb->length));
  uint8_t* ptr = pointer_to_nth_byte(sb, offset);
  (*ptr)++;
}

static alts_grpc_record_protocol_test_fixture*
test_fixture_integrity_only_create(bool rekey) {
  alts_grpc_record_protocol_test_fixture* fixture =
      static_cast<alts_grpc_record_protocol_test_fixture*>(
          gpr_zalloc(sizeof(alts_grpc_record_protocol_test_fixture)));
  size_t key_length = rekey ? kAes128GcmRekeyKeyLength : kAes128GcmKeyLength;
  uint8_t* key;
  gsec_test_random_array(&key, key_length);
  gsec_aead_crypter* crypter = nullptr;

  /* Create client record protocol for protect. */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_grpc_integrity_only_record_protocol_create(
                 crypter, 8, /*is_client=*/true, /*is_protect=*/true,
                 &fixture->client_protect) == TSI_OK);
  /* Create client record protocol for unprotect.  */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_grpc_integrity_only_record_protocol_create(
                 crypter, 8, /*is_client=*/true, /*is_protect=*/false,
                 &fixture->client_unprotect) == TSI_OK);
  /* Create server record protocol for protect.  */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_grpc_integrity_only_record_protocol_create(
                 crypter, 8, /*is_client=*/false, /*is_protect=*/true,
                 &fixture->server_protect) == TSI_OK);
  /* Create server record protocol for unprotect.  */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_grpc_integrity_only_record_protocol_create(
                 crypter, 8, /*is_client=*/false, /*is_protect=*/false,
                 &fixture->server_unprotect) == TSI_OK);

  gpr_free(key);
  return fixture;
}

static alts_grpc_record_protocol_test_fixture*
test_fixture_integrity_only_no_rekey_create() {
  return test_fixture_integrity_only_create(false);
}

static alts_grpc_record_protocol_test_fixture*
test_fixture_integrity_only_rekey_create() {
  return test_fixture_integrity_only_create(true);
}

static alts_grpc_record_protocol_test_fixture*
test_fixture_privacy_integrity_create(bool rekey) {
  alts_grpc_record_protocol_test_fixture* fixture =
      static_cast<alts_grpc_record_protocol_test_fixture*>(
          gpr_zalloc(sizeof(alts_grpc_record_protocol_test_fixture)));
  size_t key_length = rekey ? kAes128GcmRekeyKeyLength : kAes128GcmKeyLength;
  uint8_t* key;
  gsec_test_random_array(&key, key_length);
  gsec_aead_crypter* crypter = nullptr;

  /* Create client record protocol for protect. */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_grpc_privacy_integrity_record_protocol_create(
                 crypter, 8, /*is_client=*/true, /*is_protect=*/true,
                 &fixture->client_protect) == TSI_OK);
  /* Create client record protocol for unprotect.  */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_grpc_privacy_integrity_record_protocol_create(
                 crypter, 8, /*is_client=*/true, /*is_protect=*/false,
                 &fixture->client_unprotect) == TSI_OK);
  /* Create server record protocol for protect.  */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_grpc_privacy_integrity_record_protocol_create(
                 crypter, 8, /*is_client=*/false, /*is_protect=*/true,
                 &fixture->server_protect) == TSI_OK);
  /* Create server record protocol for unprotect.  */
  GPR_ASSERT(gsec_aes_gcm_aead_crypter_create(
                 key, key_length, kAesGcmNonceLength, kAesGcmTagLength, rekey,
                 &crypter, nullptr) == GRPC_STATUS_OK);
  GPR_ASSERT(alts_grpc_privacy_integrity_record_protocol_create(
                 crypter, 8, /*is_client=*/false, /*is_protect=*/false,
                 &fixture->server_unprotect) == TSI_OK);

  gpr_free(key);
  return fixture;
}

static alts_grpc_record_protocol_test_fixture*
test_fixture_privacy_integrity_no_rekey_create() {
  return test_fixture_privacy_integrity_create(false);
}

static alts_grpc_record_protocol_test_fixture*
test_fixture_privacy_integrity_rekey_create() {
  return test_fixture_privacy_integrity_create(true);
}

static void alts_grpc_record_protocol_test_fixture_destroy(
    alts_grpc_record_protocol_test_fixture* fixture) {
  if (fixture == nullptr) {
    return;
  }
  grpc_core::ExecCtx exec_ctx;
  alts_grpc_record_protocol_destroy(fixture->client_protect);
  alts_grpc_record_protocol_destroy(fixture->client_unprotect);
  alts_grpc_record_protocol_destroy(fixture->server_protect);
  alts_grpc_record_protocol_destroy(fixture->server_unprotect);
  grpc_core::ExecCtx::Get()->Flush();
  gpr_free(fixture);
}

static alts_grpc_record_protocol_test_var*
alts_grpc_record_protocol_test_var_create() {
  alts_grpc_record_protocol_test_var* var =
      static_cast<alts_grpc_record_protocol_test_var*>(
          gpr_zalloc(sizeof(alts_grpc_record_protocol_test_var)));
  var->header_length = alts_iovec_record_protocol_get_header_length();
  var->tag_length = kTagLength;
  /* Initialized slice buffers.  */
  grpc_slice_buffer_init(&var->original_sb);
  grpc_slice_buffer_init(&var->duplicate_sb);
  grpc_slice_buffer_init(&var->protected_sb);
  grpc_slice_buffer_init(&var->unprotected_sb);
  /* Randomly sets content of original_sb, and copies into duplicate_sb.  */
  create_random_slice_buffer(&var->original_sb);
  for (size_t i = 0; i < var->original_sb.count; i++) {
    grpc_slice_buffer_add(&var->duplicate_sb,
                          grpc_slice_ref(var->original_sb.slices[i]));
  }
  return var;
}

static void alts_grpc_record_protocol_test_var_destroy(
    alts_grpc_record_protocol_test_var* var) {
  if (var == nullptr) {
    return;
  }
  grpc_slice_buffer_destroy_internal(&var->original_sb);
  grpc_slice_buffer_destroy_internal(&var->duplicate_sb);
  grpc_slice_buffer_destroy_internal(&var->protected_sb);
  grpc_slice_buffer_destroy_internal(&var->unprotected_sb);
  gpr_free(var);
}

/* --- alts grpc record protocol tests. --- */

static void random_seal_unseal(alts_grpc_record_protocol* sender,
                               alts_grpc_record_protocol* receiver) {
  grpc_core::ExecCtx exec_ctx;
  for (size_t i = 0; i < kSealRepeatTimes; i++) {
    alts_grpc_record_protocol_test_var* var =
        alts_grpc_record_protocol_test_var_create();
    /* Seals and then unseals.  */
    size_t data_length = var->original_sb.length;
    tsi_result status = alts_grpc_record_protocol_protect(
        sender, &var->original_sb, &var->protected_sb);
    GPR_ASSERT(status == TSI_OK);
    GPR_ASSERT(var->protected_sb.length ==
               data_length + var->header_length + var->tag_length);
    status = alts_grpc_record_protocol_unprotect(receiver, &var->protected_sb,
                                                 &var->unprotected_sb);
    GPR_ASSERT(status == TSI_OK);
    GPR_ASSERT(
        are_slice_buffers_equal(&var->unprotected_sb, &var->duplicate_sb));
    alts_grpc_record_protocol_test_var_destroy(var);
  }
  grpc_core::ExecCtx::Get()->Flush();
}

static void empty_seal_unseal(alts_grpc_record_protocol* sender,
                              alts_grpc_record_protocol* receiver) {
  grpc_core::ExecCtx exec_ctx;
  for (size_t i = 0; i < kSealRepeatTimes; i++) {
    alts_grpc_record_protocol_test_var* var =
        alts_grpc_record_protocol_test_var_create();
    /* Seals and then unseals empty payload.  */
    grpc_slice_buffer_reset_and_unref_internal(&var->original_sb);
    grpc_slice_buffer_reset_and_unref_internal(&var->duplicate_sb);
    tsi_result status = alts_grpc_record_protocol_protect(
        sender, &var->original_sb, &var->protected_sb);
    GPR_ASSERT(status == TSI_OK);
    GPR_ASSERT(var->protected_sb.length ==
               var->header_length + var->tag_length);
    status = alts_grpc_record_protocol_unprotect(receiver, &var->protected_sb,
                                                 &var->unprotected_sb);
    GPR_ASSERT(status == TSI_OK);
    GPR_ASSERT(
        are_slice_buffers_equal(&var->unprotected_sb, &var->duplicate_sb));
    alts_grpc_record_protocol_test_var_destroy(var);
  }
  grpc_core::ExecCtx::Get()->Flush();
}

static void unsync_seal_unseal(alts_grpc_record_protocol* sender,
                               alts_grpc_record_protocol* receiver) {
  grpc_core::ExecCtx exec_ctx;
  tsi_result status;
  alts_grpc_record_protocol_test_var* var =
      alts_grpc_record_protocol_test_var_create();
  /* Seals once.  */
  status = alts_grpc_record_protocol_protect(sender, &var->original_sb,
                                             &var->protected_sb);
  GPR_ASSERT(status == TSI_OK);
  grpc_slice_buffer_reset_and_unref_internal(&var->protected_sb);
  /* Seals again.  */
  status = alts_grpc_record_protocol_protect(sender, &var->duplicate_sb,
                                             &var->protected_sb);
  GPR_ASSERT(status == TSI_OK);
  /* Unseals the second frame.  */
  status = alts_grpc_record_protocol_unprotect(receiver, &var->protected_sb,
                                               &var->unprotected_sb);
  GPR_ASSERT(status == TSI_INTERNAL_ERROR);
  alts_grpc_record_protocol_test_var_destroy(var);
  grpc_core::ExecCtx::Get()->Flush();
}

static void corrupted_data(alts_grpc_record_protocol* sender,
                           alts_grpc_record_protocol* receiver) {
  grpc_core::ExecCtx exec_ctx;
  tsi_result status;
  alts_grpc_record_protocol_test_var* var =
      alts_grpc_record_protocol_test_var_create();
  /* Seals once.  */
  status = alts_grpc_record_protocol_protect(sender, &var->original_sb,
                                             &var->protected_sb);
  GPR_ASSERT(status == TSI_OK);
  /* Corrupts one byte in protected_sb and tries to unprotect.  */
  alter_random_byte(&var->protected_sb);
  status = alts_grpc_record_protocol_unprotect(receiver, &var->protected_sb,
                                               &var->unprotected_sb);
  GPR_ASSERT(status == TSI_INTERNAL_ERROR);
  alts_grpc_record_protocol_test_var_destroy(var);
  grpc_core::ExecCtx::Get()->Flush();
}

static void input_check(alts_grpc_record_protocol* rp) {
  grpc_core::ExecCtx exec_ctx;
  tsi_result status;
  alts_grpc_record_protocol_test_var* var =
      alts_grpc_record_protocol_test_var_create();
  /* Protects with nullptr input.  */
  status = alts_grpc_record_protocol_protect(rp, nullptr, &var->protected_sb);
  GPR_ASSERT(status == TSI_INVALID_ARGUMENT);
  status = alts_grpc_record_protocol_protect(rp, &var->original_sb, nullptr);
  GPR_ASSERT(status == TSI_INVALID_ARGUMENT);
  /* Unprotects with nullptr input.  */
  status = alts_grpc_record_protocol_protect(rp, &var->original_sb,
                                             &var->protected_sb);
  GPR_ASSERT(status == TSI_OK);
  status =
      alts_grpc_record_protocol_unprotect(rp, nullptr, &var->unprotected_sb);
  GPR_ASSERT(status == TSI_INVALID_ARGUMENT);
  status = alts_grpc_record_protocol_unprotect(rp, &var->protected_sb, nullptr);
  GPR_ASSERT(status == TSI_INVALID_ARGUMENT);
  /* Unprotects on a temporary slice buffer which length is smaller than header
   * length plus tag length.  */
  grpc_slice_buffer temp_sb;
  grpc_slice_buffer_init(&temp_sb);
  grpc_slice_buffer_move_first(
      &var->protected_sb, var->header_length + var->tag_length - 1, &temp_sb);
  status =
      alts_grpc_record_protocol_unprotect(rp, &temp_sb, &var->unprotected_sb);
  GPR_ASSERT(status == TSI_INVALID_ARGUMENT);
  grpc_slice_buffer_destroy_internal(&temp_sb);
  alts_grpc_record_protocol_test_var_destroy(var);
  grpc_core::ExecCtx::Get()->Flush();
}

/* --- Test cases. --- */

static void alts_grpc_record_protocol_random_seal_unseal_tests(
    alts_grpc_record_protocol_test_fixture* fixture) {
  random_seal_unseal(fixture->client_protect, fixture->server_unprotect);
  random_seal_unseal(fixture->server_protect, fixture->client_unprotect);
}

static void alts_grpc_record_protocol_empty_seal_unseal_tests(
    alts_grpc_record_protocol_test_fixture* fixture) {
  empty_seal_unseal(fixture->client_protect, fixture->server_unprotect);
  empty_seal_unseal(fixture->server_protect, fixture->client_unprotect);
}

static void alts_grpc_record_protocol_unsync_seal_unseal_tests(
    alts_grpc_record_protocol_test_fixture* fixture) {
  unsync_seal_unseal(fixture->client_protect, fixture->server_unprotect);
  unsync_seal_unseal(fixture->server_protect, fixture->client_unprotect);
}

static void alts_grpc_record_protocol_corrupted_data_tests(
    alts_grpc_record_protocol_test_fixture* fixture) {
  corrupted_data(fixture->client_protect, fixture->server_unprotect);
  corrupted_data(fixture->server_protect, fixture->client_unprotect);
}

static void alts_grpc_record_protocol_input_check_tests(
    alts_grpc_record_protocol_test_fixture* fixture) {
  input_check(fixture->client_protect);
}

static void alts_grpc_record_protocol_tests(
    alts_grpc_record_protocol_test_fixture* (*fixture_create)()) {
  auto* fixture_1 = fixture_create();
  alts_grpc_record_protocol_random_seal_unseal_tests(fixture_1);
  alts_grpc_record_protocol_test_fixture_destroy(fixture_1);

  auto* fixture_2 = fixture_create();
  alts_grpc_record_protocol_empty_seal_unseal_tests(fixture_2);
  alts_grpc_record_protocol_test_fixture_destroy(fixture_2);

  auto* fixture_3 = fixture_create();
  alts_grpc_record_protocol_unsync_seal_unseal_tests(fixture_3);
  alts_grpc_record_protocol_test_fixture_destroy(fixture_3);

  auto* fixture_4 = fixture_create();
  alts_grpc_record_protocol_corrupted_data_tests(fixture_4);
  alts_grpc_record_protocol_test_fixture_destroy(fixture_4);

  auto* fixture_5 = fixture_create();
  alts_grpc_record_protocol_input_check_tests(fixture_5);
  alts_grpc_record_protocol_test_fixture_destroy(fixture_5);
}

int main(int argc, char** argv) {
  alts_grpc_record_protocol_tests(&test_fixture_integrity_only_no_rekey_create);
  alts_grpc_record_protocol_tests(&test_fixture_integrity_only_rekey_create);
  alts_grpc_record_protocol_tests(
      &test_fixture_privacy_integrity_no_rekey_create);
  alts_grpc_record_protocol_tests(&test_fixture_privacy_integrity_rekey_create);

  return 0;
}
