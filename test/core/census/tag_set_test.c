/*
 *
 * Copyright 2015-2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

// Test census_tag_set functions, including encoding/decoding

#include <grpc/census.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test/core/util/test_config.h"

static uint8_t one_byte_val = 7;
static uint32_t four_byte_val = 0x12345678;
static uint64_t eight_byte_val = 0x1234567890abcdef;

// A set of tags Used to create a basic tag_set for testing. Each tag has a
// unique set of flags. Note that replace_add_delete_test() relies on specific
// offsets into this array - if you add or delete entries, you will also need
// to change the test.
#define BASIC_TAG_COUNT 8
static census_tag basic_tags[BASIC_TAG_COUNT] = {
    /* 0 */ {"key0", "printable", 10, 0},
    /* 1 */ {"k1", "a", 2, CENSUS_TAG_PROPAGATE},
    /* 2 */ {"k2", "longer printable string", 24, CENSUS_TAG_STATS},
    /* 3 */ {"key_three", (char *)&one_byte_val, 1, CENSUS_TAG_BINARY},
    /* 4 */ {"really_long_key_4", "random", 7,
             CENSUS_TAG_PROPAGATE | CENSUS_TAG_STATS},
    /* 5 */ {"k5", (char *)&four_byte_val, 4,
             CENSUS_TAG_PROPAGATE | CENSUS_TAG_BINARY},
    /* 6 */ {"k6", (char *)&eight_byte_val, 8,
             CENSUS_TAG_STATS | CENSUS_TAG_BINARY},
    /* 7 */ {"k7", (char *)&four_byte_val, 4,
             CENSUS_TAG_PROPAGATE | CENSUS_TAG_STATS | CENSUS_TAG_BINARY}};

// Set of tags used to modify the basic tag_set. Note that
// replace_add_delete_test() relies on specific offsets into this array - if
// you add or delete entries, you will also need to change the test. Other
// tests that rely on specific instances have XXX_XXX_OFFSET definitions (also
// change the defines below if you add/delete entires).
#define MODIFY_TAG_COUNT 10
static census_tag modify_tags[MODIFY_TAG_COUNT] = {
#define REPLACE_VALUE_OFFSET 0
    /* 0 */ {"key0", "replace printable", 18, 0},  // replaces tag value only
#define ADD_TAG_OFFSET 1
    /* 1 */ {"new_key", "xyzzy", 6, CENSUS_TAG_STATS},  // new tag
#define DELETE_TAG_OFFSET 2
    /* 2 */ {"k5", NULL, 5,
             0},  // should delete tag, despite bogus value length
    /* 3 */ {"k6", "foo", 0, 0},  // should delete tag, despite bogus value
    /* 4 */ {"k6", "foo", 0, 0},  // try deleting already-deleted tag
    /* 5 */ {"non-existent", NULL, 0, 0},  // another non-existent tag
#define REPLACE_FLAG_OFFSET 6
    /* 6 */ {"k1", "a", 2, 0},                   // change flags only
    /* 7 */ {"k7", "bar", 4, CENSUS_TAG_STATS},  // change flags and value
    /* 8 */ {"k2", (char *)&eight_byte_val, 8,
             CENSUS_TAG_BINARY | CENSUS_TAG_PROPAGATE},  // more flags change
                                                         // non-binary -> binary
    /* 9 */ {"k6", "bar", 4,
             0}  // add back tag, with different value, but same length
};

// Utility function to compare tags. Returns true if all fields match.
static bool compare_tag(const census_tag *t1, const census_tag *t2) {
  return (strcmp(t1->key, t2->key) == 0 && t1->value_len == t2->value_len &&
          memcmp(t1->value, t2->value, t1->value_len) == 0 &&
          t1->flags == t2->flags);
}

// Utility function to validate a tag exists in tag set.
static bool validate_tag(const census_tag_set *cts, const census_tag *tag) {
  census_tag tag2;
  if (census_tag_set_get_tag_by_key(cts, tag->key, &tag2) != 1) return false;
  return compare_tag(tag, &tag2);
}

// Create an empty tag_set.
static void empty_test(void) {
  struct census_tag_set *cts = census_tag_set_create(NULL, NULL, 0, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == 0);
  census_tag_set_destroy(cts);
}

// Test create and iteration over basic tag set.
static void basic_test(void) {
  struct census_tag_set *cts =
      census_tag_set_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == BASIC_TAG_COUNT);
  census_tag_set_iterator it;
  census_tag_set_initialize_iterator(cts, &it);
  census_tag tag;
  while (census_tag_set_next_tag(&it, &tag)) {
    // can't rely on tag return order: make sure it matches exactly one.
    int matches = 0;
    for (int i = 0; i < BASIC_TAG_COUNT; i++) {
      if (compare_tag(&tag, &basic_tags[i])) matches++;
    }
    GPR_ASSERT(matches == 1);
  }
  census_tag_set_destroy(cts);
}

// Test that census_tag_set_get_tag_by_key().
static void lookup_by_key_test(void) {
  struct census_tag_set *cts =
      census_tag_set_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == BASIC_TAG_COUNT);
  census_tag tag;
  for (int i = 0; i < census_tag_set_ntags(cts); i++) {
    GPR_ASSERT(census_tag_set_get_tag_by_key(cts, basic_tags[i].key, &tag) ==
               1);
    GPR_ASSERT(compare_tag(&tag, &basic_tags[i]));
  }
  // non-existent keys
  GPR_ASSERT(census_tag_set_get_tag_by_key(cts, "key", &tag) == 0);
  GPR_ASSERT(census_tag_set_get_tag_by_key(cts, "key01", &tag) == 0);
  GPR_ASSERT(census_tag_set_get_tag_by_key(cts, "k9", &tag) == 0);
  GPR_ASSERT(census_tag_set_get_tag_by_key(cts, "random", &tag) == 0);
  GPR_ASSERT(census_tag_set_get_tag_by_key(cts, "", &tag) == 0);
  census_tag_set_destroy(cts);
}

// Try creating tag set with invalid entries.
static void invalid_test(void) {
  char key[300];
  memset(key, 'k', 299);
  key[299] = 0;
  char value[300];
  memset(value, 'v', 300);
  census_tag tag = {key, value, 3, CENSUS_TAG_BINARY};
  // long keys, short value. Key lengths (including terminator) should be
  // <= 255 (CENSUS_MAX_TAG_KV_LEN)
  GPR_ASSERT(strlen(key) == 299);
  struct census_tag_set *cts = census_tag_set_create(NULL, &tag, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == 0);
  census_tag_set_destroy(cts);
  key[CENSUS_MAX_TAG_KV_LEN] = 0;
  GPR_ASSERT(strlen(key) == CENSUS_MAX_TAG_KV_LEN);
  cts = census_tag_set_create(NULL, &tag, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == 0);
  census_tag_set_destroy(cts);
  key[CENSUS_MAX_TAG_KV_LEN - 1] = 0;
  GPR_ASSERT(strlen(key) == CENSUS_MAX_TAG_KV_LEN - 1);
  cts = census_tag_set_create(NULL, &tag, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == 1);
  census_tag_set_destroy(cts);
  // now try with long values
  tag.value_len = 300;
  cts = census_tag_set_create(NULL, &tag, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == 0);
  census_tag_set_destroy(cts);
  tag.value_len = CENSUS_MAX_TAG_KV_LEN + 1;
  cts = census_tag_set_create(NULL, &tag, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == 0);
  census_tag_set_destroy(cts);
  tag.value_len = CENSUS_MAX_TAG_KV_LEN;
  cts = census_tag_set_create(NULL, &tag, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == 1);
  census_tag_set_destroy(cts);
  // 0 length key.
  key[0] = 0;
  cts = census_tag_set_create(NULL, &tag, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == 0);
  census_tag_set_destroy(cts);
}

// Make a copy of a tag set
static void copy_test(void) {
  struct census_tag_set *cts =
      census_tag_set_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == BASIC_TAG_COUNT);
  struct census_tag_set *cts2 = census_tag_set_create(cts, NULL, 0, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts2) == BASIC_TAG_COUNT);
  for (int i = 0; i < census_tag_set_ntags(cts2); i++) {
    census_tag tag;
    GPR_ASSERT(census_tag_set_get_tag_by_key(cts2, basic_tags[i].key, &tag) ==
               1);
    GPR_ASSERT(compare_tag(&tag, &basic_tags[i]));
  }
  census_tag_set_destroy(cts);
  census_tag_set_destroy(cts2);
}

// replace a single tag value
static void replace_value_test(void) {
  struct census_tag_set *cts =
      census_tag_set_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == BASIC_TAG_COUNT);
  struct census_tag_set *cts2 =
      census_tag_set_create(cts, modify_tags + REPLACE_VALUE_OFFSET, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts2) == BASIC_TAG_COUNT);
  census_tag tag;
  GPR_ASSERT(census_tag_set_get_tag_by_key(
                 cts2, modify_tags[REPLACE_VALUE_OFFSET].key, &tag) == 1);
  GPR_ASSERT(compare_tag(&tag, &modify_tags[REPLACE_VALUE_OFFSET]));
  census_tag_set_destroy(cts);
  census_tag_set_destroy(cts2);
}

// replace a single tags flags
static void replace_flags_test(void) {
  struct census_tag_set *cts =
      census_tag_set_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == BASIC_TAG_COUNT);
  struct census_tag_set *cts2 =
      census_tag_set_create(cts, modify_tags + REPLACE_FLAG_OFFSET, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts2) == BASIC_TAG_COUNT);
  census_tag tag;
  GPR_ASSERT(census_tag_set_get_tag_by_key(
                 cts2, modify_tags[REPLACE_FLAG_OFFSET].key, &tag) == 1);
  GPR_ASSERT(compare_tag(&tag, &modify_tags[REPLACE_FLAG_OFFSET]));
  census_tag_set_destroy(cts);
  census_tag_set_destroy(cts2);
}

// delete a single tag.
static void delete_tag_test(void) {
  struct census_tag_set *cts =
      census_tag_set_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == BASIC_TAG_COUNT);
  struct census_tag_set *cts2 =
      census_tag_set_create(cts, modify_tags + DELETE_TAG_OFFSET, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts2) == BASIC_TAG_COUNT - 1);
  census_tag tag;
  GPR_ASSERT(census_tag_set_get_tag_by_key(
                 cts2, modify_tags[DELETE_TAG_OFFSET].key, &tag) == 0);
  census_tag_set_destroy(cts);
  census_tag_set_destroy(cts2);
}

// add a single new tag.
static void add_tag_test(void) {
  struct census_tag_set *cts =
      census_tag_set_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == BASIC_TAG_COUNT);
  struct census_tag_set *cts2 =
      census_tag_set_create(cts, modify_tags + ADD_TAG_OFFSET, 1, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts2) == BASIC_TAG_COUNT + 1);
  census_tag tag;
  GPR_ASSERT(census_tag_set_get_tag_by_key(
                 cts2, modify_tags[ADD_TAG_OFFSET].key, &tag) == 1);
  GPR_ASSERT(compare_tag(&tag, &modify_tags[ADD_TAG_OFFSET]));
  census_tag_set_destroy(cts);
  census_tag_set_destroy(cts2);
}

// test many changes at once.
static void replace_add_delete_test(void) {
  struct census_tag_set *cts =
      census_tag_set_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == BASIC_TAG_COUNT);
  struct census_tag_set *cts2 =
      census_tag_set_create(cts, modify_tags, MODIFY_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts2) == 8);
  // validate tag set contents. Use specific indices into the two arrays
  // holding tag values.
  GPR_ASSERT(validate_tag(cts2, &basic_tags[3]));
  GPR_ASSERT(validate_tag(cts2, &basic_tags[4]));
  GPR_ASSERT(validate_tag(cts2, &modify_tags[0]));
  GPR_ASSERT(validate_tag(cts2, &modify_tags[1]));
  GPR_ASSERT(validate_tag(cts2, &modify_tags[6]));
  GPR_ASSERT(validate_tag(cts2, &modify_tags[7]));
  GPR_ASSERT(validate_tag(cts2, &modify_tags[8]));
  GPR_ASSERT(validate_tag(cts2, &modify_tags[9]));
  GPR_ASSERT(!validate_tag(cts2, &basic_tags[0]));
  GPR_ASSERT(!validate_tag(cts2, &basic_tags[1]));
  GPR_ASSERT(!validate_tag(cts2, &basic_tags[2]));
  GPR_ASSERT(!validate_tag(cts2, &basic_tags[5]));
  GPR_ASSERT(!validate_tag(cts2, &basic_tags[6]));
  GPR_ASSERT(!validate_tag(cts2, &basic_tags[7]));
  census_tag_set_destroy(cts);
  census_tag_set_destroy(cts2);
}

// Use the basic tag set to test encode/decode.
static void simple_encode_decode_test(void) {
  char buf1[1000];
  char buf2[1000];
  struct census_tag_set *cts =
      census_tag_set_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == BASIC_TAG_COUNT);
  GPR_ASSERT(census_tag_set_encode_propagated(cts, buf1, 1) == 0);
  size_t b1 = census_tag_set_encode_propagated(cts, buf1, 1000);
  GPR_ASSERT(b1 != 0);
  GPR_ASSERT(census_tag_set_encode_propagated_binary(cts, buf2, 1) == 0);
  size_t b2 = census_tag_set_encode_propagated_binary(cts, buf2, 1000);
  GPR_ASSERT(b2 != 0);
  census_tag_set *cts2 = census_tag_set_decode(buf1, b1, buf2, b2);
  GPR_ASSERT(cts2 != NULL);
  GPR_ASSERT(census_tag_set_ntags(cts2) == 4);
  for (int i = 0; i < census_tag_set_ntags(cts); i++) {
    census_tag tag;
    if (CENSUS_TAG_IS_PROPAGATED(basic_tags[i].flags)) {
      GPR_ASSERT(census_tag_set_get_tag_by_key(cts2, basic_tags[i].key, &tag) ==
                 1);
      GPR_ASSERT(compare_tag(&tag, &basic_tags[i]));
    } else {
      GPR_ASSERT(census_tag_set_get_tag_by_key(cts2, basic_tags[i].key, &tag) ==
                 0);
    }
  }
  census_tag_set_destroy(cts2);
  census_tag_set_destroy(cts);
}

// Use more complex/modified tag set to test encode/decode.
static void complex_encode_decode_test(void) {
  char buf1[500];
  char buf2[500];
  struct census_tag_set *cts =
      census_tag_set_create(NULL, basic_tags, BASIC_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts) == BASIC_TAG_COUNT);
  struct census_tag_set *cts2 =
      census_tag_set_create(cts, modify_tags, MODIFY_TAG_COUNT, NULL);
  GPR_ASSERT(census_tag_set_ntags(cts2) == 8);

  size_t b1 = census_tag_set_encode_propagated(cts2, buf1, 500);
  GPR_ASSERT(b1 != 0);
  size_t b2 = census_tag_set_encode_propagated_binary(cts2, buf2, 500);
  GPR_ASSERT(b2 != 0);
  census_tag_set *cts3 = census_tag_set_decode(buf1, b1, buf2, b2);
  GPR_ASSERT(cts3 != NULL);
  GPR_ASSERT(census_tag_set_ntags(cts3) == 2);
  GPR_ASSERT(validate_tag(cts3, &basic_tags[4]));
  GPR_ASSERT(validate_tag(cts3, &modify_tags[8]));
  census_tag_set_destroy(cts3);
  census_tag_set_destroy(cts2);
  census_tag_set_destroy(cts);
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
  simple_encode_decode_test();
  complex_encode_decode_test();
  return 0;
}
