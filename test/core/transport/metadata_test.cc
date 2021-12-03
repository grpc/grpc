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

#include "src/core/lib/transport/metadata.h"

#include <stdio.h>
#include <string.h>

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/hpack_utils.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/core/lib/transport/static_metadata.h"
#include "test/core/util/test_config.h"

/* a large number */
#define MANY 10000

static grpc_slice maybe_intern(grpc_slice in, bool intern) {
  grpc_slice out = intern ? grpc_slice_intern(in) : grpc_slice_ref(in);
  grpc_slice_unref(in);
  return out;
}

static grpc_slice maybe_dup(grpc_slice in, bool dup) {
  grpc_slice out = dup ? grpc_slice_dup(in) : grpc_slice_ref(in);
  grpc_slice_unref(in);
  return out;
}

static void test_create_metadata(bool intern_keys, bool intern_values) {
  grpc_mdelem m1, m2, m3;

  gpr_log(GPR_INFO, "test_create_metadata: intern_keys=%d intern_values=%d",
          intern_keys, intern_values);

  grpc_core::ExecCtx exec_ctx;
  m1 = grpc_mdelem_from_slices(
      maybe_intern(grpc_slice_from_static_string("a"), intern_keys),
      maybe_intern(grpc_slice_from_static_string("b"), intern_values));
  m2 = grpc_mdelem_from_slices(
      maybe_intern(grpc_slice_from_static_string("a"), intern_keys),
      maybe_intern(grpc_slice_from_static_string("b"), intern_values));
  m3 = grpc_mdelem_from_slices(
      maybe_intern(grpc_slice_from_static_string("a"), intern_keys),
      maybe_intern(grpc_slice_from_static_string("c"), intern_values));
  GPR_ASSERT(grpc_mdelem_eq(m1, m2));
  GPR_ASSERT(!grpc_mdelem_eq(m3, m1));
  GPR_ASSERT(grpc_slice_eq(GRPC_MDKEY(m3), GRPC_MDKEY(m1)));
  GPR_ASSERT(!grpc_slice_eq(GRPC_MDVALUE(m3), GRPC_MDVALUE(m1)));
  GPR_ASSERT(grpc_slice_str_cmp(GRPC_MDKEY(m1), "a") == 0);
  GPR_ASSERT(grpc_slice_str_cmp(GRPC_MDVALUE(m1), "b") == 0);
  GPR_ASSERT(grpc_slice_str_cmp(GRPC_MDVALUE(m3), "c") == 0);
  GRPC_MDELEM_UNREF(m1);
  GRPC_MDELEM_UNREF(m2);
  GRPC_MDELEM_UNREF(m3);
}

static void test_create_many_ephemeral_metadata(bool intern_keys,
                                                bool intern_values) {
  char buffer[GPR_LTOA_MIN_BUFSIZE];
  long i;

  gpr_log(
      GPR_INFO,
      "test_create_many_ephemeral_metadata: intern_keys=%d intern_values=%d",
      intern_keys, intern_values);

  grpc_core::ExecCtx exec_ctx;
  /* add, and immediately delete a bunch of different elements */
  for (i = 0; i < MANY; i++) {
    gpr_ltoa(i, buffer);
    GRPC_MDELEM_UNREF(grpc_mdelem_from_slices(
        maybe_intern(grpc_slice_from_static_string("a"), intern_keys),
        maybe_intern(grpc_slice_from_copied_string(buffer), intern_values)));
  }
}

static void test_create_many_persistant_metadata(void) {
  char buffer[GPR_LTOA_MIN_BUFSIZE];
  long i;
  grpc_mdelem* created =
      static_cast<grpc_mdelem*>(gpr_malloc(sizeof(grpc_mdelem) * MANY));
  grpc_mdelem md;

  gpr_log(GPR_INFO, "test_create_many_persistant_metadata");

  grpc_core::ExecCtx exec_ctx;
  /* add phase */
  for (i = 0; i < MANY; i++) {
    gpr_ltoa(i, buffer);
    created[i] = grpc_mdelem_from_slices(
        grpc_slice_intern(grpc_slice_from_static_string("a")),
        grpc_slice_intern(grpc_slice_from_static_string(buffer)));
  }
  /* verify phase */
  for (i = 0; i < MANY; i++) {
    gpr_ltoa(i, buffer);
    md = grpc_mdelem_from_slices(
        grpc_slice_intern(grpc_slice_from_static_string("a")),
        grpc_slice_intern(grpc_slice_from_static_string(buffer)));
    GPR_ASSERT(grpc_mdelem_eq(md, created[i]));
    GRPC_MDELEM_UNREF(md);
  }
  /* cleanup phase */
  for (i = 0; i < MANY; i++) {
    GRPC_MDELEM_UNREF(created[i]);
  }

  gpr_free(created);
}

static void test_spin_creating_the_same_thing(bool intern_keys,
                                              bool intern_values) {
  gpr_log(GPR_INFO,
          "test_spin_creating_the_same_thing: intern_keys=%d intern_values=%d",
          intern_keys, intern_values);

  grpc_core::ExecCtx exec_ctx;
  grpc_mdelem a, b, c;
  GRPC_MDELEM_UNREF(
      a = grpc_mdelem_from_slices(
          maybe_intern(grpc_slice_from_static_string("a"), intern_keys),
          maybe_intern(grpc_slice_from_static_string("b"), intern_values)));
  GRPC_MDELEM_UNREF(
      b = grpc_mdelem_from_slices(
          maybe_intern(grpc_slice_from_static_string("a"), intern_keys),
          maybe_intern(grpc_slice_from_static_string("b"), intern_values)));
  GRPC_MDELEM_UNREF(
      c = grpc_mdelem_from_slices(
          maybe_intern(grpc_slice_from_static_string("a"), intern_keys),
          maybe_intern(grpc_slice_from_static_string("b"), intern_values)));
  if (intern_keys && intern_values) {
    GPR_ASSERT(a.payload == b.payload);
    GPR_ASSERT(a.payload == c.payload);
  }
}

static void test_identity_laws(bool intern_keys, bool intern_values) {
  gpr_log(GPR_INFO, "test_identity_laws: intern_keys=%d intern_values=%d",
          intern_keys, intern_values);

  grpc_core::ExecCtx exec_ctx;
  grpc_mdelem a, b, c;
  a = grpc_mdelem_from_slices(
      maybe_intern(grpc_slice_from_static_string("a"), intern_keys),
      maybe_intern(grpc_slice_from_static_string("b"), intern_values));
  b = grpc_mdelem_from_slices(
      maybe_intern(grpc_slice_from_static_string("a"), intern_keys),
      maybe_intern(grpc_slice_from_static_string("b"), intern_values));
  c = grpc_mdelem_from_slices(
      maybe_intern(grpc_slice_from_static_string("a"), intern_keys),
      maybe_intern(grpc_slice_from_static_string("b"), intern_values));
  GPR_ASSERT(grpc_mdelem_eq(a, a));
  GPR_ASSERT(grpc_mdelem_eq(b, b));
  GPR_ASSERT(grpc_mdelem_eq(c, c));
  GPR_ASSERT(grpc_mdelem_eq(a, b));
  GPR_ASSERT(grpc_mdelem_eq(b, c));
  GPR_ASSERT(grpc_mdelem_eq(a, c));
  GPR_ASSERT(grpc_mdelem_eq(b, a));
  GPR_ASSERT(grpc_mdelem_eq(c, b));
  GPR_ASSERT(grpc_mdelem_eq(c, a));
  if (intern_keys && intern_values) {
    GPR_ASSERT(a.payload == b.payload);
    GPR_ASSERT(a.payload == c.payload);
  } else {
    GPR_ASSERT(a.payload != b.payload);
    GPR_ASSERT(a.payload != c.payload);
    GPR_ASSERT(b.payload != c.payload);
  }
  GRPC_MDELEM_UNREF(a);
  GRPC_MDELEM_UNREF(b);
  GRPC_MDELEM_UNREF(c);
}

static void test_things_stick_around(void) {
  size_t i, j;
  size_t nstrs = 1000;
  grpc_slice* strs =
      static_cast<grpc_slice*>(gpr_malloc(sizeof(grpc_slice) * nstrs));
  size_t* shuf = static_cast<size_t*>(gpr_malloc(sizeof(size_t) * nstrs));
  grpc_slice test;

  gpr_log(GPR_INFO, "test_things_stick_around");

  grpc_core::ExecCtx exec_ctx;

  for (i = 0; i < nstrs; i++) {
    std::string buffer =
        absl::StrFormat("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx%" PRIuPTR "x", i);
    strs[i] = grpc_slice_intern(grpc_slice_from_static_string(buffer.c_str()));
    shuf[i] = i;
  }

  for (i = 0; i < nstrs; i++) {
    grpc_slice_ref_internal(strs[i]);
    grpc_slice_unref_internal(strs[i]);
  }

  for (i = 0; i < nstrs; i++) {
    size_t p = static_cast<size_t>(rand()) % nstrs;
    size_t q = static_cast<size_t>(rand()) % nstrs;
    size_t temp = shuf[p];
    shuf[p] = shuf[q];
    shuf[q] = temp;
  }

  for (i = 0; i < nstrs; i++) {
    grpc_slice_unref_internal(strs[shuf[i]]);
    for (j = i + 1; j < nstrs; j++) {
      std::string buffer = absl::StrFormat(
          "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx%" PRIuPTR "x", shuf[j]);
      test = grpc_slice_intern(grpc_slice_from_static_string(buffer.c_str()));
      GPR_ASSERT(grpc_slice_is_equivalent(test, strs[shuf[j]]));
      grpc_slice_unref_internal(test);
    }
  }
  gpr_free(strs);
  gpr_free(shuf);
}

static void test_user_data_works(void) {
  int* ud1;
  int* ud2;
  grpc_mdelem md;
  gpr_log(GPR_INFO, "test_user_data_works");

  grpc_core::ExecCtx exec_ctx;
  ud1 = static_cast<int*>(gpr_malloc(sizeof(int)));
  *ud1 = 1;
  ud2 = static_cast<int*>(gpr_malloc(sizeof(int)));
  *ud2 = 2;
  md = grpc_mdelem_from_slices(
      grpc_slice_intern(grpc_slice_from_static_string("abc")),
      grpc_slice_intern(grpc_slice_from_static_string("123")));
  grpc_mdelem_set_user_data(md, gpr_free, ud1);
  grpc_mdelem_set_user_data(md, gpr_free, ud2);
  GPR_ASSERT(grpc_mdelem_get_user_data(md, gpr_free) == ud1);
  GRPC_MDELEM_UNREF(md);
}

static void test_user_data_works_for_allocated_md(void) {
  int* ud1;
  int* ud2;
  grpc_mdelem md;
  gpr_log(GPR_INFO, "test_user_data_works");

  grpc_core::ExecCtx exec_ctx;
  ud1 = static_cast<int*>(gpr_malloc(sizeof(int)));
  *ud1 = 1;
  ud2 = static_cast<int*>(gpr_malloc(sizeof(int)));
  *ud2 = 2;
  md = grpc_mdelem_from_slices(grpc_slice_from_static_string("abc"),
                               grpc_slice_from_static_string("123"));
  grpc_mdelem_set_user_data(md, gpr_free, ud1);
  grpc_mdelem_set_user_data(md, gpr_free, ud2);
  GPR_ASSERT(grpc_mdelem_get_user_data(md, gpr_free) == ud1);
  GRPC_MDELEM_UNREF(md);
}

static void test_copied_static_metadata(bool dup_key, bool dup_value) {
  gpr_log(GPR_INFO, "test_static_metadata: dup_key=%d dup_value=%d", dup_key,
          dup_value);
  grpc_core::ExecCtx exec_ctx;

  for (size_t i = 0; i < GRPC_STATIC_MDELEM_COUNT; i++) {
    grpc_mdelem p = GRPC_MAKE_MDELEM(&grpc_core::g_static_mdelem_table[i],
                                     GRPC_MDELEM_STORAGE_STATIC);
    grpc_mdelem q =
        grpc_mdelem_from_slices(maybe_dup(GRPC_MDKEY(p), dup_key),
                                maybe_dup(GRPC_MDVALUE(p), dup_value));
    GPR_ASSERT(grpc_mdelem_eq(p, q));
    if (dup_key || dup_value) {
      GPR_ASSERT(p.payload != q.payload);
    } else {
      GPR_ASSERT(p.payload == q.payload);
    }
    GRPC_MDELEM_UNREF(p);
    GRPC_MDELEM_UNREF(q);
  }
}

static void test_grpc_metadata_batch_get_value_with_absent_key(void) {
  auto arena = grpc_core::MakeScopedArena(1024);
  grpc_metadata_batch metadata(arena.get());
  std::string concatenated_value;
  absl::optional<absl::string_view> value =
      metadata.GetValue("absent_key", &concatenated_value);
  GPR_ASSERT(value == absl::nullopt);
}

static void test_grpc_metadata_batch_get_value_returns_one_value(void) {
  const char* kKey = "some_key";
  const char* kValue = "some_value";
  auto arena = grpc_core::MakeScopedArena(1024);
  grpc_linked_mdelem storage;
  grpc_metadata_batch metadata(arena.get());
  storage.md = grpc_mdelem_from_slices(
      grpc_slice_intern(grpc_slice_from_static_string(kKey)),
      grpc_slice_intern(grpc_slice_from_static_string(kValue)));
  GPR_ASSERT(metadata.LinkHead(&storage) == GRPC_ERROR_NONE);
  std::string concatenated_value;
  absl::optional<absl::string_view> value =
      metadata.GetValue(kKey, &concatenated_value);
  GPR_ASSERT(value.has_value());
  GPR_ASSERT(value.value() == kValue);
}

static void test_grpc_metadata_batch_get_value_returns_multiple_values(void) {
  const char* kKey = "some_key";
  const char* kValue1 = "value1";
  const char* kValue2 = "value2";
  auto arena = grpc_core::MakeScopedArena(1024);
  grpc_linked_mdelem storage1;
  grpc_linked_mdelem storage2;
  grpc_metadata_batch metadata(arena.get());
  storage1.md = grpc_mdelem_from_slices(
      grpc_slice_intern(grpc_slice_from_static_string(kKey)),
      grpc_slice_intern(grpc_slice_from_static_string(kValue1)));
  GPR_ASSERT(metadata.LinkTail(&storage1) == GRPC_ERROR_NONE);
  storage2.md = grpc_mdelem_from_slices(
      grpc_slice_intern(grpc_slice_from_static_string(kKey)),
      grpc_slice_intern(grpc_slice_from_static_string(kValue2)));
  GPR_ASSERT(metadata.LinkTail(&storage2) == GRPC_ERROR_NONE);
  std::string concatenated_value;
  absl::optional<absl::string_view> value =
      metadata.GetValue(kKey, &concatenated_value);
  GPR_ASSERT(value.has_value());
  GPR_ASSERT(value.value() == absl::StrCat(kValue1, ",", kValue2));
}

static void test_grpc_chttp2_incoming_metadata_replace_or_add_works(void) {
  auto arena = grpc_core::MakeScopedArena(1024);
  grpc_metadata_batch buffer(arena.get());
  GRPC_LOG_IF_ERROR("incoming_buffer_add",
                    buffer.Append(grpc_mdelem_from_slices(
                        grpc_slice_from_static_string("a"),
                        grpc_slice_from_static_string("b"))));
  GRPC_LOG_IF_ERROR(
      "incoming_buffer_replace_or_add",
      buffer.ReplaceOrAppend(grpc_slice_from_static_string("a"),
                             grpc_slice_malloc(1024 * 1024 * 1024)));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  for (int k = 0; k <= 1; k++) {
    for (int v = 0; v <= 1; v++) {
      test_create_metadata(k, v);
      test_create_many_ephemeral_metadata(k, v);
      test_identity_laws(k, v);
      test_spin_creating_the_same_thing(k, v);
      test_copied_static_metadata(k, v);
    }
  }
  test_create_many_persistant_metadata();
  test_things_stick_around();
  test_user_data_works();
  test_user_data_works_for_allocated_md();
  test_grpc_metadata_batch_get_value_with_absent_key();
  test_grpc_metadata_batch_get_value_returns_one_value();
  test_grpc_metadata_batch_get_value_returns_multiple_values();
  test_grpc_chttp2_incoming_metadata_replace_or_add_works();
  grpc_shutdown();
  return 0;
}
