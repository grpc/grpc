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

#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#include "src/core/tsi/alts/crypt/gsec.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h"
#include "test/core/test_util/test_config.h"
#include "test/core/tsi/alts/crypt/gsec_test_util.h"
#include "gtest/gtest.h"
#include "absl/types/span.h"

constexpr size_t kMaxSliceLength = 256;
constexpr size_t kMaxSlices = 10;
constexpr size_t kSealRepeatTimes = 5;
constexpr size_t kTagLength = 16;

// Test fixtures for each test cases.
struct alts_grpc_record_protocol_test_fixture {
  alts_grpc_record_protocol* client_protect;
  alts_grpc_record_protocol* client_unprotect;
  alts_grpc_record_protocol* server_protect;
  alts_grpc_record_protocol* server_unprotect;
};

// Test input variables for protect/unprotect operations.
struct alts_grpc_record_protocol_test_var {
  size_t header_length;
  size_t tag_length;
  grpc_slice_buffer original_sb;
  grpc_slice_buffer duplicate_sb;
  grpc_slice_buffer protected_sb;
  grpc_slice_buffer unprotected_sb;
};

// --- Test utility functions. ---

static void create_random_slice_buffer(grpc_slice_buffer* sb) {
  ASSERT_NE(sb, nullptr);
  size_t slice_count = gsec_test_bias_random_uint32(kMaxSlices) + 1;
  for (size_t i = 0; i < slice_count; i++) {
    size_t slice_length = gsec_test_bias_random_uint32(kMaxSliceLength) + 1;
    grpc_slice slice = GRPC_SLICE_MALLOC(slice_length);
    gsec_test_random_bytes(GRPC_SLICE_START_PTR(slice), slice_length);
    grpc_slice_buffer_add(sb, slice);
  }
}

static uint8_t* pointer_to_nth_byte(grpc_slice_buffer* sb, size_t index) {
  EXPECT_NE(sb, nullptr);
  EXPECT_LT(index, sb->length);
  for (size_t i = 0; i < sb->count; i++) {
    if (index < GRPC_SLICE_LENGTH(sb->slices[i])) {
      return GRPC_SLICE_START_PTR(sb->slices[i]) + index;
    } else {
      index -= GRPC_SLICE_LENGTH(sb->slices[i]);
    }
  }
  return nullptr;
}

// Checks if two slice buffer contents are the same. It is not super efficient,
// but OK for testing.
static bool are_slice_buffers_equal(grpc_slice_buffer* first,
                                    grpc_slice_buffer* second) {
  EXPECT_NE(first, nullptr);
  EXPECT_NE(second, nullptr);
  if (first->length != second->length) {
    return false;
  }
  for (size_t i = 0; i < first->length; i++) {
    uint8_t* first_ptr = pointer_to_nth_byte(first, i);
    uint8_t* second_ptr = pointer_to_nth_byte(second, i);
    EXPECT_NE(first_ptr, nullptr);
    EXPECT_NE(second_ptr, nullptr);
    if ((*first_ptr) != (*second_ptr)) {
      return false;
    }
  }
  return true;
}

static void alter_random_byte(grpc_slice_buffer* sb) {
  ASSERT_NE(sb, nullptr);
  if (sb->length == 0) {
    return;
  }
  uint32_t offset =
      gsec_test_bias_random_uint32(static_cast<uint32_t>(sb->length));
  uint8_t* ptr = pointer_to_nth_byte(sb, offset);
  (*ptr)++;
}

static alts_grpc_record_protocol_test_fixture*
test_fixture_integrity_only_create(bool rekey, bool extra_copy) {
  alts_grpc_record_protocol_test_fixture* fixture =
      static_cast<alts_grpc_record_protocol_test_fixture*>(
          gpr_zalloc(sizeof(alts_grpc_record_protocol_test_fixture)));
  size_t key_length = rekey ? kAes128GcmRekeyKeyLength : kAes128GcmKeyLength;
  uint8_t* key;
  gsec_test_random_array(&key, key_length);
  gsec_aead_crypter* crypter = nullptr;

  // Create client record protocol for protect.
  EXPECT_EQ(gsec_aes_gcm_aead_crypter_create(
                std::make_unique<grpc_core::GsecKey>(
                    absl::MakeConstSpan(key, key_length), rekey),
                kAesGcmNonceLength, kAesGcmTagLength, &crypter,
                /*error_details=*/nullptr),
            GRPC_STATUS_OK);
  EXPECT_EQ(alts_grpc_integrity_only_record_protocol_create(
                crypter, 8, /*is_client=*/true, /*is_protect=*/true, extra_copy,
                &fixture->client_protect),
            TSI_OK);
  // Create client record protocol for unprotect.
  EXPECT_EQ(gsec_aes_gcm_aead_crypter_create(
                std::make_unique<grpc_core::GsecKey>(
                    absl::MakeConstSpan(key, key_length), rekey),
                kAesGcmNonceLength, kAesGcmTagLength, &crypter,
                /*error_details=*/nullptr),
            GRPC_STATUS_OK);
  EXPECT_EQ(alts_grpc_integrity_only_record_protocol_create(
                crypter, 8, /*is_client=*/true, /*is_protect=*/false,
                extra_copy, &fixture->client_unprotect),
            TSI_OK);
  // Create server record protocol for protect.
  EXPECT_EQ(gsec_aes_gcm_aead_crypter_create(
                std::make_unique<grpc_core::GsecKey>(
                    absl::MakeConstSpan(key, key_length), rekey),
                kAesGcmNonceLength, kAesGcmTagLength, &crypter,
                /*error_details=*/nullptr),
            GRPC_STATUS_OK);
  EXPECT_EQ(alts_grpc_integrity_only_record_protocol_create(
                crypter, 8, /*is_client=*/false, /*is_protect=*/true,
                extra_copy, &fixture->server_protect),
            TSI_OK);
  // Create server record protocol for unprotect.
  EXPECT_EQ(gsec_aes_gcm_aead_crypter_create(
                std::make_unique<grpc_core::GsecKey>(
                    absl::MakeConstSpan(key, key_length), rekey),
                kAesGcmNonceLength, kAesGcmTagLength, &crypter,
                /*error_details=*/nullptr),
            GRPC_STATUS_OK);
  EXPECT_EQ(alts_grpc_integrity_only_record_protocol_create(
                crypter, 8, /*is_client=*/false, /*is_protect=*/false,
                extra_copy, &fixture->server_unprotect),
            TSI_OK);

  gpr_free(key);
  return fixture;
}

static alts_grpc_record_protocol_test_fixture*
test_fixture_integrity_only_no_rekey_no_extra_copy_create() {
  return test_fixture_integrity_only_create(false, false);
}

static alts_grpc_record_protocol_test_fixture*
test_fixture_integrity_only_rekey_create() {
  return test_fixture_integrity_only_create(true, false);
}

static alts_grpc_record_protocol_test_fixture*
test_fixture_integrity_only_extra_copy_create() {
  return test_fixture_integrity_only_create(false, true);
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

  // Create client record protocol for protect.
  EXPECT_EQ(gsec_aes_gcm_aead_crypter_create(
                std::make_unique<grpc_core::GsecKey>(
                    absl::MakeConstSpan(key, key_length), rekey),
                kAesGcmNonceLength, kAesGcmTagLength, &crypter,
                /*error_details=*/nullptr),
            GRPC_STATUS_OK);
  EXPECT_EQ(alts_grpc_privacy_integrity_record_protocol_create(
                crypter, 8, /*is_client=*/true, /*is_protect=*/true,
                &fixture->client_protect),
            TSI_OK);
  // Create client record protocol for unprotect.
  EXPECT_EQ(gsec_aes_gcm_aead_crypter_create(
                std::make_unique<grpc_core::GsecKey>(
                    absl::MakeConstSpan(key, key_length), rekey),
                kAesGcmNonceLength, kAesGcmTagLength, &crypter,
                /*error_details=*/nullptr),
            GRPC_STATUS_OK);
  EXPECT_EQ(alts_grpc_privacy_integrity_record_protocol_create(
                crypter, 8, /*is_client=*/true, /*is_protect=*/false,
                &fixture->client_unprotect),
            TSI_OK);
  // Create server record protocol for protect.
  EXPECT_EQ(gsec_aes_gcm_aead_crypter_create(
                std::make_unique<grpc_core::GsecKey>(
                    absl::MakeConstSpan(key, key_length), rekey),
                kAesGcmNonceLength, kAesGcmTagLength, &crypter,
                /*error_details=*/nullptr),
            GRPC_STATUS_OK);
  EXPECT_EQ(alts_grpc_privacy_integrity_record_protocol_create(
                crypter, 8, /*is_client=*/false, /*is_protect=*/true,
                &fixture->server_protect),
            TSI_OK);
  // Create server record protocol for unprotect.
  EXPECT_EQ(gsec_aes_gcm_aead_crypter_create(
                std::make_unique<grpc_core::GsecKey>(
                    absl::MakeConstSpan(key, key_length), rekey),
                kAesGcmNonceLength, kAesGcmTagLength, &crypter,
                /*error_details=*/nullptr),
            GRPC_STATUS_OK);
  EXPECT_EQ(alts_grpc_privacy_integrity_record_protocol_create(
                crypter, 8, /*is_client=*/false, /*is_protect=*/false,
                &fixture->server_unprotect),
            TSI_OK);
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
  alts_grpc_record_protocol_destroy(fixture->client_protect);
  alts_grpc_record_protocol_destroy(fixture->client_unprotect);
  alts_grpc_record_protocol_destroy(fixture->server_protect);
  alts_grpc_record_protocol_destroy(fixture->server_unprotect);
  gpr_free(fixture);
}

static alts_grpc_record_protocol_test_var*
alts_grpc_record_protocol_test_var_create() {
  alts_grpc_record_protocol_test_var* var =
      static_cast<alts_grpc_record_protocol_test_var*>(
          gpr_zalloc(sizeof(alts_grpc_record_protocol_test_var)));
  var->header_length = alts_iovec_record_protocol_get_header_length();
  var->tag_length = kTagLength;
  // Initialized slice buffers.
  grpc_slice_buffer_init(&var->original_sb);
  grpc_slice_buffer_init(&var->duplicate_sb);
  grpc_slice_buffer_init(&var->protected_sb);
  grpc_slice_buffer_init(&var->unprotected_sb);
  // Randomly sets content of original_sb, and copies into duplicate_sb.
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
  grpc_slice_buffer_destroy(&var->original_sb);
  grpc_slice_buffer_destroy(&var->duplicate_sb);
  grpc_slice_buffer_destroy(&var->protected_sb);
  grpc_slice_buffer_destroy(&var->unprotected_sb);
  gpr_free(var);
}

// --- alts grpc record protocol tests. ---

static void random_seal_unseal(alts_grpc_record_protocol* sender,
                               alts_grpc_record_protocol* receiver) {
  for (size_t i = 0; i < kSealRepeatTimes; i++) {
    alts_grpc_record_protocol_test_var* var =
        alts_grpc_record_protocol_test_var_create();
    // Seals and then unseals.
    size_t data_length = var->original_sb.length;
    tsi_result status = alts_grpc_record_protocol_protect(
        sender, &var->original_sb, &var->protected_sb);
    ASSERT_EQ(status, TSI_OK);
    ASSERT_EQ(var->protected_sb.length,
              data_length + var->header_length + var->tag_length);
    status = alts_grpc_record_protocol_unprotect(receiver, &var->protected_sb,
                                                 &var->unprotected_sb);
    ASSERT_EQ(status, TSI_OK);
    ASSERT_TRUE(
        are_slice_buffers_equal(&var->unprotected_sb, &var->duplicate_sb));
    alts_grpc_record_protocol_test_var_destroy(var);
  }
}

static void empty_seal_unseal(alts_grpc_record_protocol* sender,
                              alts_grpc_record_protocol* receiver) {
  for (size_t i = 0; i < kSealRepeatTimes; i++) {
    alts_grpc_record_protocol_test_var* var =
        alts_grpc_record_protocol_test_var_create();
    // Seals and then unseals empty payload.
    grpc_slice_buffer_reset_and_unref(&var->original_sb);
    grpc_slice_buffer_reset_and_unref(&var->duplicate_sb);
    tsi_result status = alts_grpc_record_protocol_protect(
        sender, &var->original_sb, &var->protected_sb);
    ASSERT_EQ(status, TSI_OK);
    ASSERT_EQ(var->protected_sb.length, var->header_length + var->tag_length);
    status = alts_grpc_record_protocol_unprotect(receiver, &var->protected_sb,
                                                 &var->unprotected_sb);
    ASSERT_EQ(status, TSI_OK);
    ASSERT_TRUE(
        are_slice_buffers_equal(&var->unprotected_sb, &var->duplicate_sb));
    alts_grpc_record_protocol_test_var_destroy(var);
  }
}

static void unsync_seal_unseal(alts_grpc_record_protocol* sender,
                               alts_grpc_record_protocol* receiver) {
  tsi_result status;
  alts_grpc_record_protocol_test_var* var =
      alts_grpc_record_protocol_test_var_create();
  // Seals once.
  status = alts_grpc_record_protocol_protect(sender, &var->original_sb,
                                             &var->protected_sb);
  ASSERT_EQ(status, TSI_OK);
  grpc_slice_buffer_reset_and_unref(&var->protected_sb);
  // Seals again.
  status = alts_grpc_record_protocol_protect(sender, &var->duplicate_sb,
                                             &var->protected_sb);
  ASSERT_EQ(status, TSI_OK);
  // Unseals the second frame.
  status = alts_grpc_record_protocol_unprotect(receiver, &var->protected_sb,
                                               &var->unprotected_sb);
  ASSERT_EQ(status, TSI_INTERNAL_ERROR);
  alts_grpc_record_protocol_test_var_destroy(var);
}

static void corrupted_data(alts_grpc_record_protocol* sender,
                           alts_grpc_record_protocol* receiver) {
  tsi_result status;
  alts_grpc_record_protocol_test_var* var =
      alts_grpc_record_protocol_test_var_create();
  // Seals once.
  status = alts_grpc_record_protocol_protect(sender, &var->original_sb,
                                             &var->protected_sb);
  ASSERT_EQ(status, TSI_OK);
  // Corrupts one byte in protected_sb and tries to unprotect.
  alter_random_byte(&var->protected_sb);
  status = alts_grpc_record_protocol_unprotect(receiver, &var->protected_sb,
                                               &var->unprotected_sb);
  ASSERT_EQ(status, TSI_INTERNAL_ERROR);
  alts_grpc_record_protocol_test_var_destroy(var);
}

static void input_check(alts_grpc_record_protocol* rp) {
  tsi_result status;
  alts_grpc_record_protocol_test_var* var =
      alts_grpc_record_protocol_test_var_create();
  // Protects with nullptr input.
  status = alts_grpc_record_protocol_protect(rp, nullptr, &var->protected_sb);
  ASSERT_EQ(status, TSI_INVALID_ARGUMENT);
  status = alts_grpc_record_protocol_protect(rp, &var->original_sb, nullptr);
  ASSERT_EQ(status, TSI_INVALID_ARGUMENT);
  // Unprotects with nullptr input.
  status = alts_grpc_record_protocol_protect(rp, &var->original_sb,
                                             &var->protected_sb);
  ASSERT_EQ(status, TSI_OK);
  status =
      alts_grpc_record_protocol_unprotect(rp, nullptr, &var->unprotected_sb);
  ASSERT_EQ(status, TSI_INVALID_ARGUMENT);
  status = alts_grpc_record_protocol_unprotect(rp, &var->protected_sb, nullptr);
  ASSERT_EQ(status, TSI_INVALID_ARGUMENT);
  // Unprotects on a temporary slice buffer which length is smaller than header
  // length plus tag length.
  grpc_slice_buffer temp_sb;
  grpc_slice_buffer_init(&temp_sb);
  grpc_slice_buffer_move_first(
      &var->protected_sb, var->header_length + var->tag_length - 1, &temp_sb);
  status =
      alts_grpc_record_protocol_unprotect(rp, &temp_sb, &var->unprotected_sb);
  ASSERT_EQ(status, TSI_INVALID_ARGUMENT);
  grpc_slice_buffer_destroy(&temp_sb);
  alts_grpc_record_protocol_test_var_destroy(var);
}

// --- Test cases. ---

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

TEST(AltsGrpcRecordProtocolTest, MainTest) {
  grpc_init();
  alts_grpc_record_protocol_tests(
      &test_fixture_integrity_only_no_rekey_no_extra_copy_create);
  alts_grpc_record_protocol_tests(&test_fixture_integrity_only_rekey_create);
  alts_grpc_record_protocol_tests(
      &test_fixture_integrity_only_extra_copy_create);
  alts_grpc_record_protocol_tests(
      &test_fixture_privacy_integrity_no_rekey_create);
  alts_grpc_record_protocol_tests(&test_fixture_privacy_integrity_rekey_create);
  grpc_shutdown();
}

// --- Deterministic fragmentation, descriptor reuse and lifetime tests. ---

namespace {

std::vector<uint8_t> RandomBytes(size_t length) {
  std::vector<uint8_t> bytes(length);
  if (length > 0) {
    gsec_test_random_bytes(bytes.data(), length);
  }
  return bytes;
}

// Appends `bytes` to `sb` split into exactly `pieces` slices (empty input
// yields no slice). Every slice is non-empty, so `pieces` must not exceed
// bytes.size() unless bytes is empty.
void AppendFragmented(grpc_slice_buffer* sb, const std::vector<uint8_t>& bytes,
                      size_t pieces) {
  if (bytes.empty()) {
    ASSERT_EQ(pieces, 0u);
    return;
  }
  ASSERT_GE(pieces, 1u);
  ASSERT_LE(pieces, bytes.size());
  const size_t count_before = sb->count;
  size_t offset = 0;
  for (size_t i = 0; i < pieces; i++) {
    size_t remaining_pieces = pieces - i;
    size_t length = (bytes.size() - offset) / remaining_pieces;
    grpc_slice slice = GRPC_SLICE_MALLOC(length);
    memcpy(GRPC_SLICE_START_PTR(slice), bytes.data() + offset, length);
    // grpc_slice_buffer_add() may coalesce small slices; the _indexed variant
    // keeps exactly one descriptor per slice, which is what is under test.
    grpc_slice_buffer_add_indexed(sb, slice);
    offset += length;
  }
  ASSERT_EQ(offset, bytes.size());
  ASSERT_EQ(sb->count, count_before + pieces);
}

std::vector<uint8_t> Flatten(const grpc_slice_buffer* sb) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < sb->count; i++) {
    const uint8_t* start = GRPC_SLICE_START_PTR(sb->slices[i]);
    out.insert(out.end(), start, start + GRPC_SLICE_LENGTH(sb->slices[i]));
  }
  return out;
}

// Re-slices the content of `sb` into exactly `pieces` non-empty slices.
void Refragment(grpc_slice_buffer* sb, size_t pieces) {
  std::vector<uint8_t> bytes = Flatten(sb);
  grpc_slice_buffer_reset_and_unref(sb);
  AppendFragmented(
      sb, bytes,
      bytes.empty() ? 0 : std::clamp<size_t>(pieces, 1, bytes.size()));
}

// Keeps grpc_init()/grpc_shutdown() balanced even when an ASSERT_* returns
// early from a test body.
class AltsGrpcRecordProtocolNewTest : public ::testing::Test {
 protected:
  void SetUp() override { grpc_init(); }
  void TearDown() override { grpc_shutdown(); }
};

using FixtureCreate = alts_grpc_record_protocol_test_fixture* (*)();

const FixtureCreate kAllFixtures[] = {
    &test_fixture_integrity_only_no_rekey_no_extra_copy_create,
    &test_fixture_integrity_only_rekey_create,
    &test_fixture_integrity_only_extra_copy_create,
    &test_fixture_privacy_integrity_no_rekey_create,
    &test_fixture_privacy_integrity_rekey_create,
};

// Protects `payload` split in `protect_pieces` slices, then unprotects the
// resulting frame re-split in `unprotect_pieces` slices.
void RoundTrip(alts_grpc_record_protocol* sender,
               alts_grpc_record_protocol* receiver,
               const std::vector<uint8_t>& payload, size_t protect_pieces,
               size_t unprotect_pieces) {
  grpc_slice_buffer plain, framed, out;
  grpc_slice_buffer_init(&plain);
  grpc_slice_buffer_init(&framed);
  grpc_slice_buffer_init(&out);
  AppendFragmented(&plain, payload, protect_pieces);
  ASSERT_EQ(alts_grpc_record_protocol_protect(sender, &plain, &framed), TSI_OK);
  ASSERT_EQ(framed.length, payload.size() +
                               alts_iovec_record_protocol_get_header_length() +
                               kTagLength);
  Refragment(&framed, unprotect_pieces);
  ASSERT_EQ(alts_grpc_record_protocol_unprotect(receiver, &framed, &out),
            TSI_OK);
  EXPECT_EQ(Flatten(&out), payload);
  grpc_slice_buffer_destroy(&plain);
  grpc_slice_buffer_destroy(&framed);
  grpc_slice_buffer_destroy(&out);
}

// A crypter that delegates to a real one and can be told to fail encryption.
bool g_fail_encrypt = false;

struct FaultyCrypter {
  gsec_aead_crypter base;
  gsec_aead_crypter* inner;
};

FaultyCrypter* Faulty(const gsec_aead_crypter* c) {
  return reinterpret_cast<FaultyCrypter*>(const_cast<gsec_aead_crypter*>(c));
}

grpc_status_code FaultyEncrypt(gsec_aead_crypter* c, const uint8_t* nonce,
                               size_t nonce_length, const struct iovec* aad,
                               size_t aad_length, const struct iovec* plain,
                               size_t plain_length, struct iovec cipher,
                               size_t* written, char** error_details) {
  if (g_fail_encrypt) {
    if (error_details != nullptr) {
      *error_details = gpr_strdup("injected encrypt failure");
    }
    return GRPC_STATUS_INTERNAL;
  }
  return gsec_aead_crypter_encrypt_iovec(Faulty(c)->inner, nonce, nonce_length,
                                         aad, aad_length, plain, plain_length,
                                         cipher, written, error_details);
}

grpc_status_code FaultyDecrypt(gsec_aead_crypter* c, const uint8_t* nonce,
                               size_t nonce_length, const struct iovec* aad,
                               size_t aad_length, const struct iovec* cipher,
                               size_t cipher_length, struct iovec plain,
                               size_t* written, char** error_details) {
  return gsec_aead_crypter_decrypt_iovec(Faulty(c)->inner, nonce, nonce_length,
                                         aad, aad_length, cipher, cipher_length,
                                         plain, written, error_details);
}

grpc_status_code FaultyMaxCipher(const gsec_aead_crypter* c, size_t n,
                                 size_t* out, char** e) {
  return gsec_aead_crypter_max_ciphertext_and_tag_length(Faulty(c)->inner, n,
                                                         out, e);
}
grpc_status_code FaultyMaxPlain(const gsec_aead_crypter* c, size_t n,
                                size_t* out, char** e) {
  return gsec_aead_crypter_max_plaintext_length(Faulty(c)->inner, n, out, e);
}
grpc_status_code FaultyNonceLength(const gsec_aead_crypter* c, size_t* out,
                                   char** e) {
  return gsec_aead_crypter_nonce_length(Faulty(c)->inner, out, e);
}
grpc_status_code FaultyKeyLength(const gsec_aead_crypter* c, size_t* out,
                                 char** e) {
  return gsec_aead_crypter_key_length(Faulty(c)->inner, out, e);
}
grpc_status_code FaultyTagLength(const gsec_aead_crypter* c, size_t* out,
                                 char** e) {
  return gsec_aead_crypter_tag_length(Faulty(c)->inner, out, e);
}
void FaultyDestruct(gsec_aead_crypter* c) {
  gsec_aead_crypter_destroy(Faulty(c)->inner);
}

const gsec_aead_crypter_vtable kFaultyVtable = {
    FaultyEncrypt,     FaultyDecrypt,   FaultyMaxCipher, FaultyMaxPlain,
    FaultyNonceLength, FaultyKeyLength, FaultyTagLength, FaultyDestruct};

// Wraps a freshly created AES-GCM crypter. Ownership goes to the caller, as
// with gsec_aes_gcm_aead_crypter_create().
gsec_aead_crypter* CreateFaultyCrypter() {
  uint8_t* key;
  gsec_test_random_array(&key, kAes128GcmKeyLength);
  gsec_aead_crypter* inner = nullptr;
  EXPECT_EQ(gsec_aes_gcm_aead_crypter_create(
                std::make_unique<grpc_core::GsecKey>(
                    absl::MakeConstSpan(key, kAes128GcmKeyLength),
                    /*is_rekey=*/false),
                kAesGcmNonceLength, kAesGcmTagLength, &inner,
                /*error_details=*/nullptr),
            GRPC_STATUS_OK);
  gpr_free(key);
  auto* faulty = static_cast<FaultyCrypter*>(gpr_zalloc(sizeof(FaultyCrypter)));
  faulty->base.vtable = &kFaultyVtable;
  faulty->inner = inner;
  return &faulty->base;
}

}  // namespace

TEST_F(AltsGrpcRecordProtocolNewTest, DeterministicFragmentCounts) {
  // 0 fragments is the empty payload; 8 and 9 straddle the initial descriptor
  // capacity; 1000 forces several growth steps. The sequence deliberately goes
  // from large to small to empty on the same objects to expose stale entries.
  const size_t kPayloadFragments[] = {1, 8, 9, 1000, 3, 0, 17, 1, 0, 9};
  for (FixtureCreate create : kAllFixtures) {
    auto* fixture = create();
    for (size_t fragments : kPayloadFragments) {
      std::vector<uint8_t> payload =
          RandomBytes(fragments == 0 ? 0 : fragments * 3);
      RoundTrip(fixture->client_protect, fixture->server_unprotect, payload,
                fragments, fragments);
      RoundTrip(fixture->server_protect, fixture->client_unprotect, payload,
                fragments, 1);
    }
    alts_grpc_record_protocol_test_fixture_destroy(fixture);
  }
}

TEST_F(AltsGrpcRecordProtocolNewTest, ByteWiseSplitHeaderAndTag) {
  // Unprotect with every byte of the frame in its own slice, so that both the
  // frame header and the tag arrive split in several slices.
  for (FixtureCreate create : kAllFixtures) {
    auto* fixture = create();
    for (size_t length : {0u, 1u, 5u, 40u}) {
      std::vector<uint8_t> payload = RandomBytes(length);
      RoundTrip(fixture->client_protect, fixture->server_unprotect, payload,
                length, std::numeric_limits<size_t>::max());
    }
    alts_grpc_record_protocol_test_fixture_destroy(fixture);
  }
}

TEST_F(AltsGrpcRecordProtocolNewTest, CorruptedFragmentedTagFails) {
  for (FixtureCreate create : kAllFixtures) {
    auto* fixture = create();
    std::vector<uint8_t> payload = RandomBytes(32);
    grpc_slice_buffer plain, framed, out;
    grpc_slice_buffer_init(&plain);
    grpc_slice_buffer_init(&framed);
    grpc_slice_buffer_init(&out);
    AppendFragmented(&plain, payload, 4);
    ASSERT_EQ(alts_grpc_record_protocol_protect(fixture->client_protect, &plain,
                                                &framed),
              TSI_OK);
    std::vector<uint8_t> bytes = Flatten(&framed);
    bytes.back() ^= 0x01;  // Corrupt the last tag byte.
    grpc_slice_buffer_reset_and_unref(&framed);
    AppendFragmented(&framed, bytes, bytes.size());
    EXPECT_EQ(alts_grpc_record_protocol_unprotect(fixture->server_unprotect,
                                                  &framed, &out),
              TSI_INTERNAL_ERROR);
    grpc_slice_buffer_destroy(&plain);
    grpc_slice_buffer_destroy(&framed);
    grpc_slice_buffer_destroy(&out);
    // Destruction right after a verification failure.
    alts_grpc_record_protocol_test_fixture_destroy(fixture);
  }
}

TEST_F(AltsGrpcRecordProtocolNewTest, BoundedCopyRequiresExactLength) {
  std::vector<uint8_t> bytes = RandomBytes(10);
  grpc_slice_buffer sb;
  grpc_slice_buffer_init(&sb);
  AppendFragmented(&sb, bytes, 4);
  grpc_slice_buffer_add(&sb, grpc_empty_slice());  // Empty slices are ignored.
  std::vector<unsigned char> dst(10, 0xAA);
  EXPECT_TRUE(
      alts_grpc_record_protocol_copy_slice_buffer(&sb, absl::MakeSpan(dst)));
  EXPECT_EQ(dst, bytes);
  // Destination smaller or larger than the source is rejected, without
  // writing past the destination extent.
  std::vector<unsigned char> small(9 + 2, 0xBB);
  EXPECT_FALSE(alts_grpc_record_protocol_copy_slice_buffer(
      &sb, absl::MakeSpan(small).first(9)));
  EXPECT_EQ(small[9], 0xBB);
  EXPECT_EQ(small[10], 0xBB);
  std::vector<unsigned char> large(11, 0xCC);
  EXPECT_FALSE(
      alts_grpc_record_protocol_copy_slice_buffer(&sb, absl::MakeSpan(large)));
  grpc_slice_buffer_destroy(&sb);
  // Empty source and empty destination.
  grpc_slice_buffer empty;
  grpc_slice_buffer_init(&empty);
  EXPECT_TRUE(alts_grpc_record_protocol_copy_slice_buffer(
      &empty, absl::Span<unsigned char>()));
  grpc_slice_buffer_destroy(&empty);
}

TEST_F(AltsGrpcRecordProtocolNewTest,
       DescriptorStorageIsReusedWithoutStaleEntries) {
  auto* fixture = test_fixture_privacy_integrity_no_rekey_create();
  alts_grpc_record_protocol* rp = fixture->client_protect;
  grpc_slice_buffer big, small, none;
  grpc_slice_buffer_init(&big);
  grpc_slice_buffer_init(&small);
  grpc_slice_buffer_init(&none);
  AppendFragmented(&big, RandomBytes(300), 100);
  AppendFragmented(&small, RandomBytes(6), 3);

  absl::Span<const iovec_t> v =
      alts_grpc_record_protocol_convert_slice_buffer_to_iovec(rp, &big);
  ASSERT_EQ(v.size(), 100u);
  for (size_t i = 0; i < v.size(); i++) {
    EXPECT_EQ(v[i].iov_base, GRPC_SLICE_START_PTR(big.slices[i]));
    EXPECT_EQ(v[i].iov_len, GRPC_SLICE_LENGTH(big.slices[i]));
  }
  const size_t grown_capacity = rp->iovec_buf.capacity();
  EXPECT_GE(grown_capacity, 100u);

  v = alts_grpc_record_protocol_convert_slice_buffer_to_iovec(rp, &small);
  ASSERT_EQ(v.size(), 3u);  // No stale entries from the 100-slice conversion.
  EXPECT_EQ(rp->iovec_buf.capacity(), grown_capacity);
  for (size_t i = 0; i < v.size(); i++) {
    EXPECT_EQ(v[i].iov_base, GRPC_SLICE_START_PTR(small.slices[i]));
  }

  v = alts_grpc_record_protocol_convert_slice_buffer_to_iovec(rp, &none);
  EXPECT_EQ(v.size(), 0u);
  EXPECT_EQ(rp->iovec_buf.capacity(), grown_capacity);

  uint8_t byte = 0;
  v = alts_grpc_record_protocol_set_single_iovec(rp, iovec_t{&byte, 1});
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].iov_base, &byte);
  EXPECT_EQ(rp->iovec_buf.capacity(), grown_capacity);

  grpc_slice_buffer_destroy(&big);
  grpc_slice_buffer_destroy(&small);
  grpc_slice_buffer_destroy(&none);
  alts_grpc_record_protocol_test_fixture_destroy(fixture);
}

TEST_F(AltsGrpcRecordProtocolNewTest, IncompleteHeaderIsRejectedNotRead) {
  auto* fixture = test_fixture_privacy_integrity_no_rekey_create();
  alts_grpc_record_protocol* rp = fixture->client_unprotect;
  ASSERT_GT(rp->header_length, 2u);
  // A header shorter than header_length, in two slices: no iovec is produced.
  std::vector<uint8_t> partial = RandomBytes(rp->header_length - 1);
  AppendFragmented(&rp->header_sb, partial, 2);
  EXPECT_EQ(alts_grpc_record_protocol_get_header_iovec(rp).iov_base, nullptr);
  grpc_slice_buffer_reset_and_unref(&rp->header_sb);
  // A single slice that is too short or too long must not be handed out as a
  // header either (it would be read past its end, or silently truncated).
  AppendFragmented(&rp->header_sb, RandomBytes(rp->header_length - 1), 1);
  EXPECT_EQ(alts_grpc_record_protocol_get_header_iovec(rp).iov_base, nullptr);
  grpc_slice_buffer_reset_and_unref(&rp->header_sb);
  AppendFragmented(&rp->header_sb, RandomBytes(rp->header_length + 1), 1);
  EXPECT_EQ(alts_grpc_record_protocol_get_header_iovec(rp).iov_base, nullptr);
  grpc_slice_buffer_reset_and_unref(&rp->header_sb);
  // The complete header in several slices is flattened exactly.
  std::vector<uint8_t> full = RandomBytes(rp->header_length);
  AppendFragmented(&rp->header_sb, full, 3);
  iovec_t header = alts_grpc_record_protocol_get_header_iovec(rp);
  ASSERT_NE(header.iov_base, nullptr);
  ASSERT_EQ(header.iov_len, rp->header_length);
  EXPECT_EQ(memcmp(header.iov_base, full.data(), full.size()), 0);
  grpc_slice_buffer_reset_and_unref(&rp->header_sb);
  alts_grpc_record_protocol_test_fixture_destroy(fixture);
}

TEST_F(AltsGrpcRecordProtocolNewTest, ProtectFailureThenDestroy) {
  // The crypter fails an encryption: the protect error path must release every
  // temporary slice and leave the protocol destroyable, for each concrete type.
  struct Case {
    bool integrity_only;
    bool extra_copy;
  };
  for (Case c : {Case{true, false}, Case{true, true}, Case{false, false}}) {
    alts_grpc_record_protocol* rp = nullptr;
    gsec_aead_crypter* crypter = CreateFaultyCrypter();
    tsi_result created =
        c.integrity_only
            ? alts_grpc_integrity_only_record_protocol_create(
                  crypter, 8, /*is_client=*/true, /*is_protect=*/true,
                  c.extra_copy, &rp)
            : alts_grpc_privacy_integrity_record_protocol_create(
                  crypter, 8, /*is_client=*/true, /*is_protect=*/true, &rp);
    ASSERT_EQ(created, TSI_OK);
    for (size_t round = 0; round < 3; round++) {
      grpc_slice_buffer plain, framed;
      grpc_slice_buffer_init(&plain);
      grpc_slice_buffer_init(&framed);
      AppendFragmented(&plain, RandomBytes(64), 9);
      g_fail_encrypt = true;
      EXPECT_EQ(alts_grpc_record_protocol_protect(rp, &plain, &framed),
                TSI_INTERNAL_ERROR);
      g_fail_encrypt = false;
      EXPECT_EQ(framed.length, 0u);
      grpc_slice_buffer_destroy(&plain);
      grpc_slice_buffer_destroy(&framed);
    }
    alts_grpc_record_protocol_destroy(rp);
  }
}

TEST_F(AltsGrpcRecordProtocolNewTest, DestroyRightAfterSuccess) {
  for (FixtureCreate create : kAllFixtures) {
    auto* fixture = create();
    RoundTrip(fixture->client_protect, fixture->server_unprotect,
              RandomBytes(100), 7, 5);
    alts_grpc_record_protocol_test_fixture_destroy(fixture);
    // Create-and-destroy without any operation.
    alts_grpc_record_protocol_test_fixture_destroy(create());
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
