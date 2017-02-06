/*
 *
 * Copyright 2017, Google Inc.
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

#include <grpc/census.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include <openssl/rand.h>
#include "test/core/util/test_config.h"
// #include "src/core/ext/census/base_resources.h"
// #include "src/core/ext/census/resource.h"
// #include "test/core/util/test_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/core/ext/census/intrusive_hash_map.h"

typedef struct object {
  uint64_t val;
} object;

inline object *make_new_object(uint64_t val) {
  object *obj = (object *)gpr_malloc(sizeof(object));
  obj->val = val;
  return obj;
}

void test_empty() {
  intrusive_hash_map table;
  intrusive_hash_map_init(&table, kInitialLog2TableSize);
  GPR_ASSERT(0 == intrusive_hash_map_size(&table));
  GPR_ASSERT(true == intrusive_hash_map_empty(&table));
  intrusive_hash_map_free(&table);
}

void test_basic() {
  intrusive_hash_map table;
  intrusive_hash_map_init(&table, kInitialLog2TableSize);

  object *obj = make_new_object(20);
  ht_item *new_item = make_new_item(10, (void *)obj);
  bool ok = intrusive_hash_map_insert(&table, 10, new_item);
  GPR_ASSERT(ok);

  ht_item *item1 = intrusive_hash_map_find(&table, (uint64_t)10);
  GPR_ASSERT(((object *)item1->value)->val == 20);
  GPR_ASSERT(item1 == new_item);

  ht_item *item2 = intrusive_hash_map_erase(&table, (uint64_t)10);
  GPR_ASSERT(item2 == new_item);

  gpr_free(new_item->value);
  gpr_free(new_item);
  GPR_ASSERT(0 == intrusive_hash_map_size(&table));
  intrusive_hash_map_free(&table);
}

// Test reseting and clearing the hash table.
void test_reset_clear() {
  intrusive_hash_map table;
  intrusive_hash_map_init(&table, kInitialLog2TableSize);

  // Add some data to the table.
  for (uint64_t i = 0; i < 3; ++i) {
    object *obj = make_new_object(i);
    intrusive_hash_map_insert(&table, i, make_new_item(i, (void *)obj));
  }
  GPR_ASSERT(3 == intrusive_hash_map_size(&table));

  // Test find.
  for (uint64_t i = 0; i < 3; ++i) {
    ht_item *item = intrusive_hash_map_find(&table, i);
    GPR_ASSERT(item != NULL);
    GPR_ASSERT(item->key == i && ((object *)item->value)->val == i);
  }

  intrusive_hash_map_clear(&table);
  intrusive_hash_map_free(&table);
}

// Check that the table contains every key between [min_value, max_value]
// (inclusive).
void check_table_values(intrusive_hash_map *table, uint64_t min_value,
                        uint64_t max_value) {
  GPR_ASSERT(intrusive_hash_map_size(table) == max_value - min_value + 1);

  for (uint64_t i = min_value; i <= max_value; ++i) {
    ht_item *item = intrusive_hash_map_find(table, i);
    GPR_ASSERT(item != NULL);
    GPR_ASSERT(((object *)item->value)->val == i);
  }
}

// Add many items and cause the table to extend.
void test_extend() {
  intrusive_hash_map table;
  intrusive_hash_map_init(&table, kInitialLog2TableSize);

  const uint64_t kNumValues = (1 << 16);

  for (uint64_t i = 0; i < kNumValues; ++i) {
    object *obj = make_new_object(i);
    ht_item *item = make_new_item(i, (void *)obj);
    bool ok = intrusive_hash_map_insert(&table, (uint64_t)i, item);
    GPR_ASSERT(ok);
    if (i % 1000 == 0) {
      check_table_values(&table, 0, i);
    }
  }

  for (uint64_t i = 0; i < kNumValues; ++i) {
    ht_item *item = intrusive_hash_map_find(&table, i);
    GPR_ASSERT(item != NULL);
    GPR_ASSERT(item->key == i && ((object *)item->value)->val == i);
    ht_item *item2 = intrusive_hash_map_erase(&table, i);
    GPR_ASSERT(item == item2);
    gpr_free(item->value);
    gpr_free(item);
  }

  GPR_ASSERT(intrusive_hash_map_empty(&table));
  intrusive_hash_map_free(&table);
}

void test_stress() {
  uint32_t random;
  intrusive_hash_map table;
  intrusive_hash_map_init(&table, kInitialLog2TableSize);
  size_t n = 0;

  for (uint64_t i = 0; i < 1000000; ++i) {
    RAND_bytes((uint8_t *)&random, 4);
    int op = random % 2;

    switch (op) {
      case 0: {
        RAND_bytes((uint8_t *)&random, 4);
        uint64_t key = random % 10000;
        object *obj = make_new_object(key);
        ht_item *item = make_new_item(key, (void *)obj);
        bool ok = intrusive_hash_map_insert(&table, key, item);
        if (ok) {
          n++;
        } else {
          gpr_free(item->value);
          gpr_free(item);
        }
        break;
      }
      case 1: {
        RAND_bytes((uint8_t *)&random, 4);
        uint64_t key = random % 10000;
        ht_item *item = intrusive_hash_map_find(&table, key);
        if (item != NULL) {
          n--;
          GPR_ASSERT(key == ((object *)item->value)->val);
          ht_item *item2 = intrusive_hash_map_erase(&table, key);
          GPR_ASSERT(item == item2);
          gpr_free(item->value);
          gpr_free(item);
        }
        break;
      }
    }
  }
  // Check size
  GPR_ASSERT(n == intrusive_hash_map_size(&table));

  // Clean the table up.
  intrusive_hash_map_clear(&table);
  GPR_ASSERT(intrusive_hash_map_empty(&table));
  intrusive_hash_map_free(&table);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  test_empty();
  test_basic();
  test_reset_clear();
  test_extend();
  test_stress();

  return 0;
}
