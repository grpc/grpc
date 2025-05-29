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

#include "src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.h"

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>

#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "src/core/tsi/alts/crypt/gsec.h"
#include "src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.h"
#include "src/core/tsi/transport_security_grpc.h"
#include "test/core/test_util/test_config.h"
#include "test/core/tsi/alts/crypt/gsec_test_util.h"

// TODO(unknown): tests zero_copy_grpc_protector under TSI test library, which
// has more comprehensive tests.

constexpr size_t kSealRepeatTimes = 50;
constexpr size_t kSmallBufferSize = 16;
constexpr size_t kLargeBufferSize = 16384;
constexpr size_t kChannelMaxSize = 2048;
constexpr size_t kChannelMinSize = 128;
constexpr size_t kOverhead = 24;
constexpr size_t kMaxProtectedFrameSize = 1024;

// Test fixtures for each test cases.
struct alts_zero_copy_grpc_protector_test_fixture {
  tsi_zero_copy_grpc_protector* client;
  tsi_zero_copy_grpc_protector* server;
};

// Test input variables for protect/unprotect operations.
struct alts_zero_copy_grpc_protector_test_var {
  grpc_slice_buffer original_sb;
  grpc_slice_buffer duplicate_sb;
  grpc_slice_buffer staging_sb;
  grpc_slice_buffer protected_sb;
  grpc_slice_buffer unprotected_sb;
};

// --- Test utility functions. ---

static void create_random_slice_buffer(grpc_slice_buffer* sb,
                                       grpc_slice_buffer* dup_sb,
                                       size_t length) {
  ASSERT_NE(sb, nullptr);
  ASSERT_NE(dup_sb, nullptr);
  ASSERT_GT(length, 0);
  grpc_slice slice = GRPC_SLICE_MALLOC(length);
  gsec_test_random_bytes(GRPC_SLICE_START_PTR(slice), length);
  grpc_slice_buffer_add(sb, grpc_slice_ref(slice));
  grpc_slice_buffer_add(dup_sb, slice);
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
    EXPECT_TRUE(first_ptr != nullptr && second_ptr != nullptr);
    if ((*first_ptr) != (*second_ptr)) {
      return false;
    }
  }
  return true;
}

static alts_zero_copy_grpc_protector_test_fixture*
alts_zero_copy_grpc_protector_test_fixture_create(bool rekey,
                                                  bool integrity_only,
                                                  bool enable_extra_copy) {
  alts_zero_copy_grpc_protector_test_fixture* fixture =
      static_cast<alts_zero_copy_grpc_protector_test_fixture*>(
          gpr_zalloc(sizeof(alts_zero_copy_grpc_protector_test_fixture)));
  size_t key_length = rekey ? kAes128GcmRekeyKeyLength : kAes128GcmKeyLength;
  uint8_t* key;
  size_t max_protected_frame_size = kMaxProtectedFrameSize;
  size_t actual_max_protected_frame_size;
  gsec_test_random_array(&key, key_length);
  EXPECT_EQ(alts_zero_copy_grpc_protector_create(
                grpc_core::GsecKeyFactory(absl::MakeConstSpan(key, key_length),
                                          rekey),
                /*is_client=*/true, integrity_only, enable_extra_copy,
                &max_protected_frame_size, &fixture->client),
            TSI_OK);
  EXPECT_EQ(tsi_zero_copy_grpc_protector_max_frame_size(
                fixture->client, &actual_max_protected_frame_size),
            TSI_OK);
  EXPECT_EQ(actual_max_protected_frame_size, max_protected_frame_size);
  EXPECT_EQ(alts_zero_copy_grpc_protector_create(
                grpc_core::GsecKeyFactory(absl::MakeConstSpan(key, key_length),
                                          rekey),
                /*is_client=*/false, integrity_only, enable_extra_copy,
                &max_protected_frame_size, &fixture->server),
            TSI_OK);
  EXPECT_EQ(tsi_zero_copy_grpc_protector_max_frame_size(
                fixture->server, &actual_max_protected_frame_size),
            TSI_OK);
  EXPECT_EQ(actual_max_protected_frame_size, max_protected_frame_size);
  gpr_free(key);
  return fixture;
}

static void alts_zero_copy_grpc_protector_test_fixture_destroy(
    alts_zero_copy_grpc_protector_test_fixture* fixture) {
  if (fixture == nullptr) {
    return;
  }
  tsi_zero_copy_grpc_protector_destroy(fixture->client);
  tsi_zero_copy_grpc_protector_destroy(fixture->server);
  gpr_free(fixture);
}

static alts_zero_copy_grpc_protector_test_var*
alts_zero_copy_grpc_protector_test_var_create() {
  alts_zero_copy_grpc_protector_test_var* var =
      static_cast<alts_zero_copy_grpc_protector_test_var*>(
          gpr_zalloc(sizeof(alts_zero_copy_grpc_protector_test_var)));
  grpc_slice_buffer_init(&var->original_sb);
  grpc_slice_buffer_init(&var->duplicate_sb);
  grpc_slice_buffer_init(&var->staging_sb);
  grpc_slice_buffer_init(&var->protected_sb);
  grpc_slice_buffer_init(&var->unprotected_sb);
  return var;
}

static void alts_zero_copy_grpc_protector_test_var_destroy(
    alts_zero_copy_grpc_protector_test_var* var) {
  if (var == nullptr) {
    return;
  }
  grpc_slice_buffer_destroy(&var->original_sb);
  grpc_slice_buffer_destroy(&var->duplicate_sb);
  grpc_slice_buffer_destroy(&var->staging_sb);
  grpc_slice_buffer_destroy(&var->protected_sb);
  grpc_slice_buffer_destroy(&var->unprotected_sb);
  gpr_free(var);
}

// --- ALTS zero-copy protector tests. ---

static void seal_unseal_small_buffer(tsi_zero_copy_grpc_protector* sender,
                                     tsi_zero_copy_grpc_protector* receiver,
                                     bool do_frame_size_read = false) {
  for (size_t i = 0; i < kSealRepeatTimes; i++) {
    int min_progress_size;
    alts_zero_copy_grpc_protector_test_var* var =
        alts_zero_copy_grpc_protector_test_var_create();
    // Creates a random small slice buffer and calls protect().
    create_random_slice_buffer(&var->original_sb, &var->duplicate_sb,
                               kSmallBufferSize);
    ASSERT_EQ(tsi_zero_copy_grpc_protector_protect(sender, &var->original_sb,
                                                   &var->protected_sb),
              TSI_OK);
    // Splits protected slice buffer into two: first one is staging_sb, and
    // second one is protected_sb.
    uint32_t staging_sb_size =
        gsec_test_bias_random_uint32(
            static_cast<uint32_t>(var->protected_sb.length - 1)) +
        1;
    grpc_slice_buffer_move_first(&var->protected_sb, staging_sb_size,
                                 &var->staging_sb);
    // TODO(gtcooke94) call the test with and without this
    if (do_frame_size_read) {
      uint32_t frame_size;
      ASSERT_TRUE(tsi_zero_copy_grpc_protector_read_frame_size(
          receiver, &var->staging_sb, &frame_size));
      EXPECT_EQ(frame_size, kSmallBufferSize + kOverhead);
    }
    // Unprotects one by one.
    ASSERT_EQ(tsi_zero_copy_grpc_protector_unprotect(receiver, &var->staging_sb,
                                                     &var->unprotected_sb,
                                                     &min_progress_size),
              TSI_OK);
    if (staging_sb_size >= kZeroCopyFrameLengthFieldSize) {
      ASSERT_EQ(min_progress_size, static_cast<int>(var->protected_sb.length));
    } else {
      ASSERT_EQ(min_progress_size, 1);
    }
    ASSERT_EQ(var->unprotected_sb.length, 0);
    ASSERT_EQ(tsi_zero_copy_grpc_protector_unprotect(
                  receiver, &var->protected_sb, &var->unprotected_sb,
                  &min_progress_size),
              TSI_OK);
    ASSERT_TRUE(
        are_slice_buffers_equal(&var->unprotected_sb, &var->duplicate_sb));
    ASSERT_EQ(min_progress_size, 1);
    alts_zero_copy_grpc_protector_test_var_destroy(var);
  }
}

static void seal_unseal_large_buffer(tsi_zero_copy_grpc_protector* sender,
                                     tsi_zero_copy_grpc_protector* receiver,
                                     bool do_frame_size_read = false) {
  for (size_t i = 0; i < kSealRepeatTimes; i++) {
    alts_zero_copy_grpc_protector_test_var* var =
        alts_zero_copy_grpc_protector_test_var_create();
    // Creates a random large slice buffer and calls protect().
    create_random_slice_buffer(&var->original_sb, &var->duplicate_sb,
                               kLargeBufferSize);
    ASSERT_EQ(tsi_zero_copy_grpc_protector_protect(sender, &var->original_sb,
                                                   &var->protected_sb),
              TSI_OK);
    // Splits protected slice buffer into multiple pieces. Receiver unprotects
    // each slice buffer one by one.
    uint32_t channel_size = gsec_test_bias_random_uint32(static_cast<uint32_t>(
                                kChannelMaxSize + 1 - kChannelMinSize)) +
                            static_cast<uint32_t>(kChannelMinSize);
    while (var->protected_sb.length > channel_size) {
      grpc_slice_buffer_reset_and_unref(&var->staging_sb);
      grpc_slice_buffer_move_first(&var->protected_sb, channel_size,
                                   &var->staging_sb);
      if (do_frame_size_read) {
        uint32_t frame_size;
        ASSERT_TRUE(tsi_zero_copy_grpc_protector_read_frame_size(
            receiver, &var->staging_sb, &frame_size));
        EXPECT_EQ(frame_size, kMaxProtectedFrameSize + 4);
      }
      ASSERT_EQ(tsi_zero_copy_grpc_protector_unprotect(
                    receiver, &var->staging_sb, &var->unprotected_sb, nullptr),
                TSI_OK);
    }
    ASSERT_EQ(tsi_zero_copy_grpc_protector_unprotect(
                  receiver, &var->protected_sb, &var->unprotected_sb, nullptr),
              TSI_OK);
    ASSERT_TRUE(
        are_slice_buffers_equal(&var->unprotected_sb, &var->duplicate_sb));
    alts_zero_copy_grpc_protector_test_var_destroy(var);
  }
}

// --- Test cases. ---

static void alts_zero_copy_protector_seal_unseal_small_buffer_tests(
    bool enable_extra_copy) {
  alts_zero_copy_grpc_protector_test_fixture* fixture =
      alts_zero_copy_grpc_protector_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/true, enable_extra_copy);
  seal_unseal_small_buffer(fixture->client, fixture->server);
  seal_unseal_small_buffer(fixture->server, fixture->client);
  seal_unseal_small_buffer(fixture->client, fixture->server,
                           /*do_frame_size_read=*/true);
  seal_unseal_small_buffer(fixture->server, fixture->client,
                           /*do_frame_size_read=*/true);
  alts_zero_copy_grpc_protector_test_fixture_destroy(fixture);

  fixture = alts_zero_copy_grpc_protector_test_fixture_create(
      /*rekey=*/false, /*integrity_only=*/false, enable_extra_copy);
  seal_unseal_small_buffer(fixture->client, fixture->server);
  seal_unseal_small_buffer(fixture->server, fixture->client);
  seal_unseal_small_buffer(fixture->client, fixture->server,
                           /*do_frame_size_read=*/true);
  seal_unseal_small_buffer(fixture->server, fixture->client,
                           /*do_frame_size_read=*/true);
  alts_zero_copy_grpc_protector_test_fixture_destroy(fixture);

  fixture = alts_zero_copy_grpc_protector_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/true, enable_extra_copy);
  seal_unseal_small_buffer(fixture->client, fixture->server);
  seal_unseal_small_buffer(fixture->server, fixture->client);
  seal_unseal_small_buffer(fixture->client, fixture->server,
                           /*do_frame_size_read=*/true);
  seal_unseal_small_buffer(fixture->server, fixture->client,
                           /*do_frame_size_read=*/true);
  alts_zero_copy_grpc_protector_test_fixture_destroy(fixture);

  fixture = alts_zero_copy_grpc_protector_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/false, enable_extra_copy);
  seal_unseal_small_buffer(fixture->client, fixture->server);
  seal_unseal_small_buffer(fixture->server, fixture->client);
  seal_unseal_small_buffer(fixture->client, fixture->server,
                           /*do_frame_size_read=*/true);
  seal_unseal_small_buffer(fixture->server, fixture->client,
                           /*do_frame_size_read=*/true);
  alts_zero_copy_grpc_protector_test_fixture_destroy(fixture);
}

static void alts_zero_copy_protector_seal_unseal_large_buffer_tests(
    bool enable_extra_copy) {
  alts_zero_copy_grpc_protector_test_fixture* fixture =
      alts_zero_copy_grpc_protector_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/true, enable_extra_copy);
  seal_unseal_large_buffer(fixture->client, fixture->server);
  seal_unseal_large_buffer(fixture->server, fixture->client);
  seal_unseal_large_buffer(fixture->client, fixture->server,
                           /*do_frame_size_read=*/true);
  seal_unseal_large_buffer(fixture->server, fixture->client,
                           /*do_frame_size_read=*/true);
  alts_zero_copy_grpc_protector_test_fixture_destroy(fixture);

  fixture = alts_zero_copy_grpc_protector_test_fixture_create(
      /*rekey=*/false, /*integrity_only=*/false, enable_extra_copy);
  seal_unseal_large_buffer(fixture->client, fixture->server);
  seal_unseal_large_buffer(fixture->server, fixture->client);
  seal_unseal_large_buffer(fixture->client, fixture->server,
                           /*do_frame_size_read=*/true);
  seal_unseal_large_buffer(fixture->server, fixture->client,
                           /*do_frame_size_read=*/true);
  alts_zero_copy_grpc_protector_test_fixture_destroy(fixture);

  fixture = alts_zero_copy_grpc_protector_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/true, enable_extra_copy);
  seal_unseal_large_buffer(fixture->client, fixture->server);
  seal_unseal_large_buffer(fixture->server, fixture->client);
  seal_unseal_large_buffer(fixture->client, fixture->server,
                           /*do_frame_size_read=*/true);
  seal_unseal_large_buffer(fixture->server, fixture->client,
                           /*do_frame_size_read=*/true);
  alts_zero_copy_grpc_protector_test_fixture_destroy(fixture);

  fixture = alts_zero_copy_grpc_protector_test_fixture_create(
      /*rekey=*/true, /*integrity_only=*/false, enable_extra_copy);
  seal_unseal_large_buffer(fixture->client, fixture->server);
  seal_unseal_large_buffer(fixture->server, fixture->client);
  seal_unseal_large_buffer(fixture->client, fixture->server,
                           /*do_frame_size_read=*/true);
  seal_unseal_large_buffer(fixture->server, fixture->client,
                           /*do_frame_size_read=*/true);
  alts_zero_copy_grpc_protector_test_fixture_destroy(fixture);
}

TEST(AltsZeroCopyGrpcProtectorTest, MainTest) {
  grpc_init();
  alts_zero_copy_protector_seal_unseal_small_buffer_tests(
      /*enable_extra_copy=*/false);
  alts_zero_copy_protector_seal_unseal_small_buffer_tests(
      /*enable_extra_copy=*/true);
  alts_zero_copy_protector_seal_unseal_large_buffer_tests(
      /*enable_extra_copy=*/false);
  alts_zero_copy_protector_seal_unseal_large_buffer_tests(
      /*enable_extra_copy=*/true);
  grpc_shutdown();
}

TEST(AltsZeroCopyFrameProtectorTest, ReadFrameSizeSuccesSmall) {
  alts_zero_copy_grpc_protector_test_fixture* fixture =
      alts_zero_copy_grpc_protector_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/false, false);
  alts_zero_copy_grpc_protector_test_var* var =
      alts_zero_copy_grpc_protector_test_var_create();
  // Creates a random small slice buffer and calls protect().
  create_random_slice_buffer(&var->original_sb, &var->duplicate_sb,
                             kSmallBufferSize);
  ASSERT_EQ(tsi_zero_copy_grpc_protector_protect(
                fixture->client, &var->original_sb, &var->protected_sb),
            TSI_OK);
  uint32_t frame_size;
  ASSERT_TRUE(tsi_zero_copy_grpc_protector_read_frame_size(
      fixture->server, &var->protected_sb, &frame_size));
  EXPECT_EQ(frame_size, kSmallBufferSize + kOverhead);
  // // Splits protected slice buffer into two: first one is staging_sb, and
  // // second one is protected_sb.
  // uint32_t staging_sb_size =
  // gsec_test_bias_random_uint32(static_cast<uint32_t>(
  //                                var->protected_sb.length - 1)) +
  //                            1;
  // grpc_slice_buffer_move_first(&var->protected_sb, staging_sb_size,
  //                              &var->staging_sb);
}

TEST(AltsZeroCopyFrameProtectorTest, ReadFrameSizeSuccesLarge) {
  alts_zero_copy_grpc_protector_test_fixture* fixture =
      alts_zero_copy_grpc_protector_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/false, false);
  alts_zero_copy_grpc_protector_test_var* var =
      alts_zero_copy_grpc_protector_test_var_create();
  // Creates a random small slice buffer and calls protect().
  create_random_slice_buffer(&var->original_sb, &var->duplicate_sb,
                             kLargeBufferSize);
  ASSERT_EQ(tsi_zero_copy_grpc_protector_protect(
                fixture->client, &var->original_sb, &var->protected_sb),
            TSI_OK);
  uint32_t frame_size;
  ASSERT_TRUE(tsi_zero_copy_grpc_protector_read_frame_size(
      fixture->server, &var->protected_sb, &frame_size));
  EXPECT_EQ(frame_size, kMaxProtectedFrameSize + 4);
}

TEST(AltsZeroCopyFrameProtectorTest,
     ReadFrameSizeCallingTwiceDoesNotChangeValue) {
  alts_zero_copy_grpc_protector_test_fixture* fixture =
      alts_zero_copy_grpc_protector_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/false, false);
  alts_zero_copy_grpc_protector_test_var* var =
      alts_zero_copy_grpc_protector_test_var_create();
  // Creates a random small slice buffer and calls protect().
  create_random_slice_buffer(&var->original_sb, &var->duplicate_sb,
                             kSmallBufferSize);
  ASSERT_EQ(tsi_zero_copy_grpc_protector_protect(
                fixture->client, &var->original_sb, &var->protected_sb),
            TSI_OK);
  // Splits protected slice buffer into two: first one is staging_sb, and
  // second one is protected_sb.
  // The first 4 bytes are the size, so splitting to 10 will be valid to read the frame size from.
  uint32_t staging_sb_size = 10;
  grpc_slice_buffer_move_first(&var->protected_sb, staging_sb_size,
                               &var->staging_sb);
  uint32_t frame_size;
  ASSERT_TRUE(tsi_zero_copy_grpc_protector_read_frame_size(
      fixture->server, &var->staging_sb, &frame_size));
  EXPECT_EQ(frame_size, kSmallBufferSize + kOverhead);
  // Call with the other slice with different size, but it won't change the value or even read this.
  ASSERT_TRUE(tsi_zero_copy_grpc_protector_read_frame_size(
      fixture->server, &var->protected_sb, &frame_size));
  EXPECT_EQ(frame_size, kSmallBufferSize + kOverhead);
}

TEST(AltsZeroCopyFrameProtectorTest, ReadFrameSizeHeaderIsSplit) {
  alts_zero_copy_grpc_protector_test_fixture* fixture =
      alts_zero_copy_grpc_protector_test_fixture_create(
          /*rekey=*/false, /*integrity_only=*/false, false);
  alts_zero_copy_grpc_protector_test_var* var =
      alts_zero_copy_grpc_protector_test_var_create();
  // Creates a random small slice buffer and calls protect().
  create_random_slice_buffer(&var->original_sb, &var->duplicate_sb,
                             kSmallBufferSize);
  ASSERT_EQ(tsi_zero_copy_grpc_protector_protect(
                fixture->client, &var->original_sb, &var->protected_sb),
            TSI_OK);
  // Header is 4 bytes, split this so it can't read the size
  uint32_t staging_sb_size = 2;
  grpc_slice_buffer_move_first(&var->protected_sb, staging_sb_size,
                               &var->staging_sb);
  uint32_t frame_size;
  EXPECT_FALSE(tsi_zero_copy_grpc_protector_read_frame_size(
      fixture->server, &var->staging_sb, &frame_size));
  // Append the split slice to the slice buffer so it can read the size
  grpc_slice_buffer_add(&var->staging_sb,
                        grpc_slice_buffer_take_first(&var->protected_sb));
  ASSERT_TRUE(tsi_zero_copy_grpc_protector_read_frame_size(
      fixture->server, &var->staging_sb, &frame_size));
  EXPECT_EQ(frame_size, kSmallBufferSize + kOverhead);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
