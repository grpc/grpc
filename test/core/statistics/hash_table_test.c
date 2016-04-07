/*
 *
 * Copyright 2015, Google Inc.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/ext/census/hash_table.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include "src/core/lib/support/murmur_hash.h"
#include "src/core/lib/support/string.h"
#include "test/core/util/test_config.h"

static uint64_t hash64(const void *k) {
  size_t len = strlen(k);
  uint64_t higher = gpr_murmur_hash3((const char *)k, len / 2, 0);
  return higher << 32 |
         gpr_murmur_hash3((const char *)(k) + len / 2, len - len / 2, 0);
}

static int cmp_str_keys(const void *k1, const void *k2) {
  return strcmp((const char *)k1, (const char *)k2);
}

static uint64_t force_collision(const void *k) {
  return (1997 + hash64(k) % 3);
}

static void free_data(void *data) { gpr_free(data); }

/* Basic tests that empty hash table can be created and destroyed. */
static void test_create_table(void) {
  /* Create table with uint64 key type */
  census_ht *ht = NULL;
  census_ht_option ht_options = {
      CENSUS_HT_UINT64, 1999, NULL, NULL, NULL, NULL};
  ht = census_ht_create(&ht_options);
  GPR_ASSERT(ht != NULL);
  GPR_ASSERT(census_ht_get_size(ht) == 0);
  census_ht_destroy(ht);
  /* Create table with pointer key type */
  ht = NULL;
  ht_options.key_type = CENSUS_HT_POINTER;
  ht_options.hash = &hash64;
  ht_options.compare_keys = &cmp_str_keys;
  ht = census_ht_create(&ht_options);
  GPR_ASSERT(ht != NULL);
  GPR_ASSERT(census_ht_get_size(ht) == 0);
  census_ht_destroy(ht);
}

static void test_table_with_int_key(void) {
  census_ht_option opt = {CENSUS_HT_UINT64, 7, NULL, NULL, NULL, NULL};
  census_ht *ht = census_ht_create(&opt);
  uint64_t i = 0;
  uint64_t sum_of_keys = 0;
  size_t num_elements;
  census_ht_kv *elements = NULL;
  GPR_ASSERT(ht != NULL);
  GPR_ASSERT(census_ht_get_size(ht) == 0);
  elements = census_ht_get_all_elements(ht, &num_elements);
  GPR_ASSERT(num_elements == 0);
  GPR_ASSERT(elements == NULL);
  for (i = 0; i < 20; ++i) {
    census_ht_key key;
    key.val = i;
    census_ht_insert(ht, key, (void *)(intptr_t)i);
    GPR_ASSERT(census_ht_get_size(ht) == i + 1);
  }
  for (i = 0; i < 20; i++) {
    uint64_t *val = NULL;
    census_ht_key key;
    key.val = i;
    val = census_ht_find(ht, key);
    GPR_ASSERT(val == (void *)(intptr_t)i);
  }
  elements = census_ht_get_all_elements(ht, &num_elements);
  GPR_ASSERT(elements != NULL);
  GPR_ASSERT(num_elements == 20);
  for (i = 0; i < num_elements; i++) {
    sum_of_keys += elements[i].k.val;
  }
  GPR_ASSERT(sum_of_keys == 190);
  gpr_free(elements);
  census_ht_destroy(ht);
}

/* Test that there is no memory leak when keys and values are owned by table. */
static void test_value_and_key_deleter(void) {
  census_ht_option opt = {CENSUS_HT_POINTER, 7,          &hash64,
                          &cmp_str_keys,     &free_data, &free_data};
  census_ht *ht = census_ht_create(&opt);
  census_ht_key key;
  char *val = NULL;
  char *val2 = NULL;
  key.ptr = gpr_malloc(100);
  val = gpr_malloc(10);
  strcpy(val, "value");
  strcpy(key.ptr, "some string as a key");
  GPR_ASSERT(ht != NULL);
  GPR_ASSERT(census_ht_get_size(ht) == 0);
  census_ht_insert(ht, key, val);
  GPR_ASSERT(census_ht_get_size(ht) == 1);
  val = census_ht_find(ht, key);
  GPR_ASSERT(val != NULL);
  GPR_ASSERT(strcmp(val, "value") == 0);
  /* Insert same key different value, old value is overwritten. */
  val2 = gpr_malloc(10);
  strcpy(val2, "v2");
  census_ht_insert(ht, key, val2);
  GPR_ASSERT(census_ht_get_size(ht) == 1);
  val2 = census_ht_find(ht, key);
  GPR_ASSERT(val2 != NULL);
  GPR_ASSERT(strcmp(val2, "v2") == 0);
  census_ht_destroy(ht);
}

/* Test simple insert and erase operations. */
static void test_simple_add_and_erase(void) {
  census_ht_option opt = {CENSUS_HT_UINT64, 7, NULL, NULL, NULL, NULL};
  census_ht *ht = census_ht_create(&opt);
  GPR_ASSERT(ht != NULL);
  GPR_ASSERT(census_ht_get_size(ht) == 0);
  {
    census_ht_key key;
    int val = 3;
    key.val = 2;
    census_ht_insert(ht, key, (void *)&val);
    GPR_ASSERT(census_ht_get_size(ht) == 1);
    census_ht_erase(ht, key);
    GPR_ASSERT(census_ht_get_size(ht) == 0);
    /* Erasing a key from an empty table should be noop. */
    census_ht_erase(ht, key);
    GPR_ASSERT(census_ht_get_size(ht) == 0);
    /* Erasing a non-existant key from a table should be noop. */
    census_ht_insert(ht, key, (void *)&val);
    key.val = 3;
    census_ht_insert(ht, key, (void *)&val);
    key.val = 9;
    census_ht_insert(ht, key, (void *)&val);
    GPR_ASSERT(census_ht_get_size(ht) == 3);
    key.val = 1;
    census_ht_erase(ht, key);
    /* size unchanged after deleting non-existant key. */
    GPR_ASSERT(census_ht_get_size(ht) == 3);
    /* size decrease by 1 after deleting an existant key. */
    key.val = 2;
    census_ht_erase(ht, key);
    GPR_ASSERT(census_ht_get_size(ht) == 2);
  }
  census_ht_destroy(ht);
}

static void test_insertion_and_deletion_with_high_collision_rate(void) {
  census_ht_option opt = {CENSUS_HT_POINTER, 13,   &force_collision,
                          &cmp_str_keys,     NULL, NULL};
  census_ht *ht = census_ht_create(&opt);
  char key_str[1000][GPR_LTOA_MIN_BUFSIZE];
  uint64_t val = 0;
  unsigned i = 0;
  for (i = 0; i < 1000; i++) {
    census_ht_key key;
    key.ptr = key_str[i];
    gpr_ltoa(i, key_str[i]);
    census_ht_insert(ht, key, (void *)(&val));
    gpr_log(GPR_INFO, "%d\n", i);
    GPR_ASSERT(census_ht_get_size(ht) == (i + 1));
  }
  for (i = 0; i < 1000; i++) {
    census_ht_key key;
    key.ptr = key_str[i];
    census_ht_erase(ht, key);
    GPR_ASSERT(census_ht_get_size(ht) == (999 - i));
  }
  census_ht_destroy(ht);
}

static void test_table_with_string_key(void) {
  census_ht_option opt = {CENSUS_HT_POINTER, 7,    &hash64,
                          &cmp_str_keys,     NULL, NULL};
  census_ht *ht = census_ht_create(&opt);
  const char *keys[] = {
      "k1", "a",   "000", "apple", "banana_a_long_long_long_banana",
      "%$", "111", "foo", "b"};
  const int vals[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  int i = 0;
  GPR_ASSERT(ht != NULL);
  GPR_ASSERT(census_ht_get_size(ht) == 0);
  for (i = 0; i < 9; i++) {
    census_ht_key key;
    key.ptr = (void *)(keys[i]);
    census_ht_insert(ht, key, (void *)(vals + i));
  }
  GPR_ASSERT(census_ht_get_size(ht) == 9);
  for (i = 0; i < 9; i++) {
    census_ht_key key;
    int *val_ptr;
    key.ptr = (void *)(keys[i]);
    val_ptr = census_ht_find(ht, key);
    GPR_ASSERT(*val_ptr == vals[i]);
  }
  {
    /* inserts duplicate keys */
    census_ht_key key;
    int *val_ptr = NULL;
    key.ptr = (void *)(keys[2]);
    census_ht_insert(ht, key, (void *)(vals + 8));
    /* expect value to be over written by new insertion */
    GPR_ASSERT(census_ht_get_size(ht) == 9);
    val_ptr = census_ht_find(ht, key);
    GPR_ASSERT(*val_ptr == vals[8]);
  }
  for (i = 0; i < 9; i++) {
    census_ht_key key;
    int *val_ptr;
    uint32_t expected_tbl_sz = 9 - i;
    GPR_ASSERT(census_ht_get_size(ht) == expected_tbl_sz);
    key.ptr = (void *)(keys[i]);
    val_ptr = census_ht_find(ht, key);
    GPR_ASSERT(val_ptr != NULL);
    census_ht_erase(ht, key);
    GPR_ASSERT(census_ht_get_size(ht) == expected_tbl_sz - 1);
    val_ptr = census_ht_find(ht, key);
    GPR_ASSERT(val_ptr == NULL);
  }
  census_ht_destroy(ht);
}

static void test_insertion_with_same_key(void) {
  census_ht_option opt = {CENSUS_HT_UINT64, 11, NULL, NULL, NULL, NULL};
  census_ht *ht = census_ht_create(&opt);
  census_ht_key key;
  const char vals[] = {'a', 'b', 'c'};
  char *val_ptr;
  key.val = 3;
  census_ht_insert(ht, key, (void *)&(vals[0]));
  GPR_ASSERT(census_ht_get_size(ht) == 1);
  val_ptr = (char *)census_ht_find(ht, key);
  GPR_ASSERT(val_ptr != NULL);
  GPR_ASSERT(*val_ptr == 'a');
  key.val = 4;
  val_ptr = (char *)census_ht_find(ht, key);
  GPR_ASSERT(val_ptr == NULL);
  key.val = 3;
  census_ht_insert(ht, key, (void *)&(vals[1]));
  GPR_ASSERT(census_ht_get_size(ht) == 1);
  val_ptr = (char *)census_ht_find(ht, key);
  GPR_ASSERT(val_ptr != NULL);
  GPR_ASSERT(*val_ptr == 'b');
  census_ht_insert(ht, key, (void *)&(vals[2]));
  GPR_ASSERT(census_ht_get_size(ht) == 1);
  val_ptr = (char *)census_ht_find(ht, key);
  GPR_ASSERT(val_ptr != NULL);
  GPR_ASSERT(*val_ptr == 'c');
  census_ht_destroy(ht);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_create_table();
  test_simple_add_and_erase();
  test_table_with_int_key();
  test_table_with_string_key();
  test_value_and_key_deleter();
  test_insertion_with_same_key();
  test_insertion_and_deletion_with_high_collision_rate();
  return 0;
}
