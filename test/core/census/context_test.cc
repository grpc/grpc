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

// Test census_context functions, including encoding/decoding

#include <grpc/census.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test/core/util/test_config.h"

// A set of tags Used to create a basic context for testing. Note that
// replace_add_delete_test() relies on specific offsets into this array - if
// you add or delete entries, you will also need to change the test.
#define BASIC_TAG_COUNT 8
static census_tag basic_tags[BASIC_TAG_COUNT] = {
    /* 0 */ {"key0", "tag value", 0},
    /* 1 */ {"k1", "a", CENSUS_TAG_PROPAGATE},
    /* 2 */ {"k2", "a longer tag value supercalifragilisticexpialiadocious",
             CENSUS_TAG_STATS},
    /* 3 */ {"key_three", "", 0},
    /* 4 */ {"a_really_really_really_really_long_key_4", "random",
             CENSUS_TAG_PROPAGATE | CENSUS_TAG_STATS},
    /* 5 */ {"k5", "v5", CENSUS_TAG_PROPAGATE},
    /* 6 */ {"k6", "v6", CENSUS_TAG_STATS},
    /* 7 */ {"k7", "v7", CENSUS_TAG_PROPAGATE | CENSUS_TAG_STATS}};

// Set of tags used to modify the basic context. Note that
// replace_add_delete_test() relies on specific offsets into this array - if
// you add or delete entries, you will also need to change the test. Other
// tests that rely on specific instances have XXX_XXX_OFFSET definitions (also
// change the defines below if you add/delete entires).
#define MODIFY_TAG_COUNT 10
static census_tag modify_tags[MODIFY_TAG_COUNT] = {
#define REPLACE_VALUE_OFFSET 0
    /* 0 */ {"key0", "replace key0", 0},  // replaces tag value only
#define ADD_TAG_OFFSET 1
    /* 1 */ {"new_key", "xyzzy", CENSUS_TAG_STATS},  // new tag
#define DELETE_TAG_OFFSET 2
    /* 2 */ {"k5", NULL, 0},            // should delete tag
    /* 3 */ {"k5", NULL, 0},            // try deleting already-deleted tag
    /* 4 */ {"non-existent", NULL, 0},  // delete non-existent tag
#define REPLACE_FLAG_OFFSET 5
    /* 5 */ {"k1", "a", 0},                    // change flags only
    /* 6 */ {"k7", "bar", CENSUS_TAG_STATS},   // change flags and value
    /* 7 */ {"k2", "", CENSUS_TAG_PROPAGATE},  // more value and flags change
    /* 8 */ {"k5", "bar", 0},  // add back tag, with different value
    /* 9 */ {"foo", "bar", CENSUS_TAG_PROPAGATE},  // another new tag
};

// Utility function to compare tags. Returns true if all fields match.
static bool compare_tag(const census_tag *t1, const census_tag *t2) {
  return (strcmp(t1->key, t2->key) == 0 && strcmp(t1->value, t2->value) == 0 &&
          t1->flags == t2->flags);
}

// Utility function to validate a tag exists in context.
static bool validate_tag(const census_context *context, const census_tag *tag) {
  census_tag tag2;
  if (census_context_get_tag(context, tag->key, &tag2) != 1) return false;
  return compare_tag(tag, &tag2);
}

// Create an empty context.
static void empty_test(void) {
  struct census_context *context = census_context_create(NULL, NULL, 0, NULL);
  GPR_ASSERT(context != NULL);
  const census_context_status *status = census_context_get_status(context);
  census_context_status expected = {0, 0, 0, 0, 0, 0, 0};
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_context_destroy(context);
}

// Test create and iteration over basic context.
static void basic_test(void) {
  const census_context_status *status;
  struct census_context *context =
      census_context_create(NULL, basic_tags, BASIC_TAG_COUNT, &status);
  census_context_status expected = {4, 4, 0, 8, 0, 0, 0};
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_context_iterator it;
  census_context_initialize_iterator(context, &it);
  census_tag tag;
  while (census_context_next_tag(&it, &tag)) {
    // can't rely on tag return order: make sure it matches exactly one.
    int matches = 0;
    for (int i = 0; i < BASIC_TAG_COUNT; i++) {
      if (compare_tag(&tag, &basic_tags[i])) matches++;
    }
    GPR_ASSERT(matches == 1);
  }
  census_context_destroy(context);
}

// Test census_context_get_tag().
static void lookup_by_key_test(void) {
  struct census_context *context =
      census_context_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  census_tag tag;
  for (int i = 0; i < BASIC_TAG_COUNT; i++) {
    GPR_ASSERT(census_context_get_tag(context, basic_tags[i].key, &tag) == 1);
    GPR_ASSERT(compare_tag(&tag, &basic_tags[i]));
  }
  // non-existent keys
  GPR_ASSERT(census_context_get_tag(context, "key", &tag) == 0);
  GPR_ASSERT(census_context_get_tag(context, "key01", &tag) == 0);
  GPR_ASSERT(census_context_get_tag(context, "k9", &tag) == 0);
  GPR_ASSERT(census_context_get_tag(context, "random", &tag) == 0);
  GPR_ASSERT(census_context_get_tag(context, "", &tag) == 0);
  census_context_destroy(context);
}

// Try creating context with invalid entries.
static void invalid_test(void) {
  char key[300];
  memset(key, 'k', 299);
  key[299] = 0;
  char value[300];
  memset(value, 'v', 299);
  value[299] = 0;
  census_tag tag = {key, value, 0};
  // long keys, short value. Key lengths (including terminator) should be
  // <= 255 (CENSUS_MAX_TAG_KV_LEN)
  value[3] = 0;
  GPR_ASSERT(strlen(value) == 3);
  GPR_ASSERT(strlen(key) == 299);
  const census_context_status *status;
  struct census_context *context =
      census_context_create(NULL, &tag, 1, &status);
  census_context_status expected = {0, 0, 0, 0, 0, 1, 0};
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_context_destroy(context);
  key[CENSUS_MAX_TAG_KV_LEN] = 0;
  GPR_ASSERT(strlen(key) == CENSUS_MAX_TAG_KV_LEN);
  context = census_context_create(NULL, &tag, 1, &status);
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_context_destroy(context);
  key[CENSUS_MAX_TAG_KV_LEN - 1] = 0;
  GPR_ASSERT(strlen(key) == CENSUS_MAX_TAG_KV_LEN - 1);
  context = census_context_create(NULL, &tag, 1, &status);
  census_context_status expected2 = {0, 1, 0, 1, 0, 0, 0};
  GPR_ASSERT(memcmp(status, &expected2, sizeof(expected2)) == 0);
  census_context_destroy(context);
  // now try with long values
  value[3] = 'v';
  GPR_ASSERT(strlen(value) == 299);
  context = census_context_create(NULL, &tag, 1, &status);
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_context_destroy(context);
  value[CENSUS_MAX_TAG_KV_LEN] = 0;
  GPR_ASSERT(strlen(value) == CENSUS_MAX_TAG_KV_LEN);
  context = census_context_create(NULL, &tag, 1, &status);
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_context_destroy(context);
  value[CENSUS_MAX_TAG_KV_LEN - 1] = 0;
  GPR_ASSERT(strlen(value) == CENSUS_MAX_TAG_KV_LEN - 1);
  context = census_context_create(NULL, &tag, 1, &status);
  GPR_ASSERT(memcmp(status, &expected2, sizeof(expected2)) == 0);
  census_context_destroy(context);
  // 0 length key.
  key[0] = 0;
  GPR_ASSERT(strlen(key) == 0);
  context = census_context_create(NULL, &tag, 1, &status);
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_context_destroy(context);
  // invalid key character
  key[0] = 31;  // 32 (' ') is the first valid character value
  key[1] = 0;
  GPR_ASSERT(strlen(key) == 1);
  context = census_context_create(NULL, &tag, 1, &status);
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_context_destroy(context);
  // invalid value character
  key[0] = ' ';
  value[5] = 127;  // 127 (DEL) is ('~' + 1)
  value[8] = 0;
  GPR_ASSERT(strlen(key) == 1);
  GPR_ASSERT(strlen(value) == 8);
  context = census_context_create(NULL, &tag, 1, &status);
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_context_destroy(context);
}

// Make a copy of a context
static void copy_test(void) {
  struct census_context *context =
      census_context_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  const census_context_status *status;
  struct census_context *context2 =
      census_context_create(context, NULL, 0, &status);
  census_context_status expected = {4, 4, 0, 0, 0, 0, 0};
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  for (int i = 0; i < BASIC_TAG_COUNT; i++) {
    census_tag tag;
    GPR_ASSERT(census_context_get_tag(context2, basic_tags[i].key, &tag) == 1);
    GPR_ASSERT(compare_tag(&tag, &basic_tags[i]));
  }
  census_context_destroy(context);
  census_context_destroy(context2);
}

// replace a single tag value
static void replace_value_test(void) {
  struct census_context *context =
      census_context_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  const census_context_status *status;
  struct census_context *context2 = census_context_create(
      context, modify_tags + REPLACE_VALUE_OFFSET, 1, &status);
  census_context_status expected = {4, 4, 0, 0, 1, 0, 0};
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_tag tag;
  GPR_ASSERT(census_context_get_tag(
                 context2, modify_tags[REPLACE_VALUE_OFFSET].key, &tag) == 1);
  GPR_ASSERT(compare_tag(&tag, &modify_tags[REPLACE_VALUE_OFFSET]));
  census_context_destroy(context);
  census_context_destroy(context2);
}

// replace a single tags flags
static void replace_flags_test(void) {
  struct census_context *context =
      census_context_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  const census_context_status *status;
  struct census_context *context2 = census_context_create(
      context, modify_tags + REPLACE_FLAG_OFFSET, 1, &status);
  census_context_status expected = {3, 5, 0, 0, 1, 0, 0};
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_tag tag;
  GPR_ASSERT(census_context_get_tag(
                 context2, modify_tags[REPLACE_FLAG_OFFSET].key, &tag) == 1);
  GPR_ASSERT(compare_tag(&tag, &modify_tags[REPLACE_FLAG_OFFSET]));
  census_context_destroy(context);
  census_context_destroy(context2);
}

// delete a single tag.
static void delete_tag_test(void) {
  struct census_context *context =
      census_context_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  const census_context_status *status;
  struct census_context *context2 = census_context_create(
      context, modify_tags + DELETE_TAG_OFFSET, 1, &status);
  census_context_status expected = {3, 4, 1, 0, 0, 0, 0};
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_tag tag;
  GPR_ASSERT(census_context_get_tag(
                 context2, modify_tags[DELETE_TAG_OFFSET].key, &tag) == 0);
  census_context_destroy(context);
  census_context_destroy(context2);
}

// add a single new tag.
static void add_tag_test(void) {
  struct census_context *context =
      census_context_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  const census_context_status *status;
  struct census_context *context2 =
      census_context_create(context, modify_tags + ADD_TAG_OFFSET, 1, &status);
  census_context_status expected = {4, 5, 0, 1, 0, 0, 0};
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  census_tag tag;
  GPR_ASSERT(census_context_get_tag(context2, modify_tags[ADD_TAG_OFFSET].key,
                                    &tag) == 1);
  GPR_ASSERT(compare_tag(&tag, &modify_tags[ADD_TAG_OFFSET]));
  census_context_destroy(context);
  census_context_destroy(context2);
}

// test many changes at once.
static void replace_add_delete_test(void) {
  struct census_context *context =
      census_context_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  const census_context_status *status;
  struct census_context *context2 =
      census_context_create(context, modify_tags, MODIFY_TAG_COUNT, &status);
  census_context_status expected = {3, 7, 1, 3, 4, 0, 0};
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  // validate context contents. Use specific indices into the two arrays
  // holding tag values.
  GPR_ASSERT(validate_tag(context2, &basic_tags[3]));
  GPR_ASSERT(validate_tag(context2, &basic_tags[4]));
  GPR_ASSERT(validate_tag(context2, &basic_tags[6]));
  GPR_ASSERT(validate_tag(context2, &modify_tags[0]));
  GPR_ASSERT(validate_tag(context2, &modify_tags[1]));
  GPR_ASSERT(validate_tag(context2, &modify_tags[5]));
  GPR_ASSERT(validate_tag(context2, &modify_tags[6]));
  GPR_ASSERT(validate_tag(context2, &modify_tags[7]));
  GPR_ASSERT(validate_tag(context2, &modify_tags[8]));
  GPR_ASSERT(validate_tag(context2, &modify_tags[9]));
  GPR_ASSERT(!validate_tag(context2, &basic_tags[0]));
  GPR_ASSERT(!validate_tag(context2, &basic_tags[1]));
  GPR_ASSERT(!validate_tag(context2, &basic_tags[2]));
  GPR_ASSERT(!validate_tag(context2, &basic_tags[5]));
  GPR_ASSERT(!validate_tag(context2, &basic_tags[7]));
  census_context_destroy(context);
  census_context_destroy(context2);
}

#define BUF_SIZE 200

// test encode/decode.
static void encode_decode_test(void) {
  char buffer[BUF_SIZE];
  struct census_context *context =
      census_context_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  // Test with too small a buffer
  GPR_ASSERT(census_context_encode(context, buffer, 2) == 0);
  // Test with sufficient buffer
  size_t buf_used = census_context_encode(context, buffer, BUF_SIZE);
  GPR_ASSERT(buf_used != 0);
  census_context *context2 = census_context_decode(buffer, buf_used);
  GPR_ASSERT(context2 != NULL);
  const census_context_status *status = census_context_get_status(context2);
  census_context_status expected = {4, 0, 0, 0, 0, 0, 0};
  GPR_ASSERT(memcmp(status, &expected, sizeof(expected)) == 0);
  for (int i = 0; i < BASIC_TAG_COUNT; i++) {
    census_tag tag;
    if (CENSUS_TAG_IS_PROPAGATED(basic_tags[i].flags)) {
      GPR_ASSERT(census_context_get_tag(context2, basic_tags[i].key, &tag) ==
                 1);
      GPR_ASSERT(compare_tag(&tag, &basic_tags[i]));
    } else {
      GPR_ASSERT(census_context_get_tag(context2, basic_tags[i].key, &tag) ==
                 0);
    }
  }
  census_context_destroy(context2);
  census_context_destroy(context);
}

int main(int argc, char *argv[]) {
  grpc_test_init(argc, argv);
  empty_test();
  basic_test();
  lookup_by_key_test();
  invalid_test();
  copy_test();
  replace_value_test();
  replace_flags_test();
  delete_tag_test();
  add_tag_test();
  replace_add_delete_test();
  encode_decode_test();
  return 0;
}
