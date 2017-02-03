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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
// #include "src/core/ext/census/base_resources.h"
// #include "src/core/ext/census/resource.h"
// #include "test/core/util/test_config.h"

#include "src/core/ext/census/intrusive_hash_map.h"

void test_empty() {
  intrusive_hash_map table;
  intrusive_hash_map_init(&table);
  GPR_ASSERT(0 == intrusive_hash_map_size(&table));
  GPR_ASSERT(true == intrusive_hash_map_empty(&table));
}

void test_basic() {
  intrusive_hash_map table;
  intrusive_hash_map_init(&table);

  ht_item *new_item = make_new_item(10, 20);
  ht_item *p = intrusive_hash_map_insert(&table, 10, new_item);
  GPR_ASSERT(p->key == 10 && p->value == 20);
  GPR_ASSERT(p == new_item);

  ht_index idx1;
  ht_item *item1 = intrusive_hash_map_find(&table, 10, &idx1);
  GPR_ASSERT(item1->value == 20);
  GPR_ASSERT(item1 == new_item);

  ht_item *item2 = intrusive_hash_map_erase(&table, 10);
  GPR_ASSERT(item3 == new_item);

  gpr_free(new_item);

  GPR_ASSERT(0 == intrusive_hash_map_size(&table));
}

// Test reseting and clearing the hash table.
void test_reset_clear() {
  intrusive_hash_map table;
  intrusive_hash_map_init(&table);

  // Add some data to the table.
  for (int i = 0; i < 3; ++i) {
    intrusive_hash_map_insert(&table, i, make_new_item(i, i));
  }
  GPR_ASSERT(3 == intrusive_hash_map_size(&table));

  // Test find.
  for (int i = 0; i < 3; ++i) {
    ht_item *item = intrusive_hash_map_find(&table, i);
    GPR_ASSERT(item != NULL);
    GPR_ASSERT(item->key == i && item->value == i);
  }

  intrusive_hash_map_clear(&table);
}

// Check that the table contains every key between [min_value,
// max_value] (inclusive).
void check_table_values(intrusive_hash_map *hash, int min_value, int max_value) {
  GPR_ASSERT(intrusive_hash_map_size(&table) == max_value - min_value + 1);

  for (int i=min_value; i <= max_value; ++i) {
    ht_item *item = intrusive_hash_map_find(&table, i);
    GPR_ASSERT(item != NULL);
    GPR_ASSERT(item->value >= min_value);
    GPR_ASSERT(item->value <= max_value);
  }
}

// Add many items and cause the table to extend.
void test_extend() {
  intrusive_hash_map table;
  intrusive_hash_map_init(&table);

  static const int kNumValues = (1 << 16);

  for (int i = 0; i < kNumValues; ++i) {
    Item *item = make_new_item(i,i);
    bool ok = intrusive_hash_map_insert(&table, i, item);
    GPR_ASSERT(ok);
    if (i % 1000 == 0) {
      CheckTableValues(&table, 0, i);
    }
  }

  for (int i = 0; i < kNumValues; ++i) {
    ht_item *item = intrusive_hash_map_find(&table, i);
    GPR_ASSERT(item != NULL);
    GPR_ASSERT(item->key == i && item->value == i);
    intrusive_hash_map_erase(&table, i);
    gpr_free(item);
  }

  GPR_ASSERT(table.empty());
}

void test_stress() {
  uint32_t random;
  intrusive_hash_map table;
  intrusive_hash_map_init(&table);

  for (int i = 0; i < 1000000; ++i) {
    RAND_bytes(&random, 4);
    int op = random % 2;
    int n = 0;

    switch (op) {
      case 0: {
        RAND_bytes(&random, 4);
        int key = random % 10000;
        ht_item *item = make_new_item(key, key);
        bool ok = intrusive_hash_map_insert(&table, key, item);
        if (ok) {
          n++;
        } else {
          gpr_free(item);
        }
        break;
      }
      case 1: {
        RAND_bytes(&random, 4);
        int key = random % 10000;
        ht_item *item = intrusive_hash_map_find(&table, key);

        if(item != NULL) {
          GPR_ASSERT(key == item->value);
          ht_item *item2 = intrusive_hash_map_erase(&table, key);
          GPR_ASSERT(item == item2);
          gpr_free(item);
        }
        break;
      }
    }
  }

  // Clean the table up.
  intrusive_hash_map_clear(&table);
  GPR_ASSERT(intrusive_hash_map_empty(&table));
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
