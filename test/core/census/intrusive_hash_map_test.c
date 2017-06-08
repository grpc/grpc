/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/ext/census/intrusive_hash_map.h"

#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The initial size of an intrusive hash map will be 2 to this power. */
static const uint32_t kInitialLog2Size = 4;

/* Simple object used for testing intrusive_hash_map. */
typedef struct object { uint64_t val; } object;

/* Helper function to allocate and initialize object. */
static __inline object *make_new_object(uint64_t val) {
  object *obj = (object *)gpr_malloc(sizeof(object));
  obj->val = val;
  return obj;
}

/* Wrapper struct for object. */
typedef struct ptr_item {
  INTRUSIVE_HASH_MAP_HEADER;
  object *obj;
} ptr_item;

/* Helper function that creates a new hash map item.  It is up to the user to
 * free the item that was allocated. */
static __inline ptr_item *make_ptr_item(uint64_t key, uint64_t value) {
  ptr_item *new_item = (ptr_item *)gpr_malloc(sizeof(ptr_item));
  new_item->IHM_key = key;
  new_item->IHM_hash_link = NULL;
  new_item->obj = make_new_object(value);
  return new_item;
}

/* Helper function to deallocate ptr_item. */
static void free_ptr_item(void *ptr) { gpr_free(((ptr_item *)ptr)->obj); }

/* Simple string object used for testing intrusive_hash_map. */
typedef struct string_item {
  INTRUSIVE_HASH_MAP_HEADER;
  // User data.
  char buf[32];
  uint16_t len;
} string_item;

/* Helper function to allocate and initialize string object. */
static string_item *make_string_item(uint64_t key, const char *buf,
                                     uint16_t len) {
  string_item *item = (string_item *)gpr_malloc(sizeof(string_item));
  item->IHM_key = key;
  item->IHM_hash_link = NULL;
  item->len = len;
  memcpy(item->buf, buf, sizeof(char) * len);
  return item;
}

/* Helper function for comparing two string objects. */
static bool compare_string_item(const string_item *A, const string_item *B) {
  if (A->IHM_key != B->IHM_key || A->len != B->len)
    return false;
  else {
    for (int i = 0; i < A->len; ++i) {
      if (A->buf[i] != B->buf[i]) return false;
    }
  }

  return true;
}

void test_empty() {
  intrusive_hash_map hash_map;
  intrusive_hash_map_init(&hash_map, kInitialLog2Size);
  GPR_ASSERT(0 == intrusive_hash_map_size(&hash_map));
  GPR_ASSERT(intrusive_hash_map_empty(&hash_map));
  intrusive_hash_map_free(&hash_map, NULL);
}

void test_single_item() {
  intrusive_hash_map hash_map;
  intrusive_hash_map_init(&hash_map, kInitialLog2Size);

  ptr_item *new_item = make_ptr_item(10, 20);
  bool ok = intrusive_hash_map_insert(&hash_map, (hm_item *)new_item);
  GPR_ASSERT(ok);

  ptr_item *item1 =
      (ptr_item *)intrusive_hash_map_find(&hash_map, (uint64_t)10);
  GPR_ASSERT(item1->obj->val == 20);
  GPR_ASSERT(item1 == new_item);

  ptr_item *item2 =
      (ptr_item *)intrusive_hash_map_erase(&hash_map, (uint64_t)10);
  GPR_ASSERT(item2 == new_item);

  gpr_free(new_item->obj);
  gpr_free(new_item);
  GPR_ASSERT(0 == intrusive_hash_map_size(&hash_map));
  intrusive_hash_map_free(&hash_map, &free_ptr_item);
}

void test_two_items() {
  intrusive_hash_map hash_map;
  intrusive_hash_map_init(&hash_map, kInitialLog2Size);

  string_item *new_item1 = make_string_item(10, "test1", 5);
  bool ok = intrusive_hash_map_insert(&hash_map, (hm_item *)new_item1);
  GPR_ASSERT(ok);
  string_item *new_item2 = make_string_item(20, "test2", 5);
  ok = intrusive_hash_map_insert(&hash_map, (hm_item *)new_item2);
  GPR_ASSERT(ok);

  string_item *item1 =
      (string_item *)intrusive_hash_map_find(&hash_map, (uint64_t)10);
  GPR_ASSERT(compare_string_item(new_item1, item1));
  GPR_ASSERT(item1 == new_item1);
  string_item *item2 =
      (string_item *)intrusive_hash_map_find(&hash_map, (uint64_t)20);
  GPR_ASSERT(compare_string_item(new_item2, item2));
  GPR_ASSERT(item2 == new_item2);

  item1 = (string_item *)intrusive_hash_map_erase(&hash_map, (uint64_t)10);
  GPR_ASSERT(item1 == new_item1);
  item2 = (string_item *)intrusive_hash_map_erase(&hash_map, (uint64_t)20);
  GPR_ASSERT(item2 == new_item2);

  gpr_free(new_item1);
  gpr_free(new_item2);
  GPR_ASSERT(0 == intrusive_hash_map_size(&hash_map));
  intrusive_hash_map_free(&hash_map, NULL);
}

// Test resetting and clearing the hash map.
void test_reset_clear() {
  intrusive_hash_map hash_map;
  intrusive_hash_map_init(&hash_map, kInitialLog2Size);

  // Add some data to the hash_map.
  for (uint64_t i = 0; i < 3; ++i) {
    intrusive_hash_map_insert(&hash_map, (hm_item *)make_ptr_item(i, i));
  }
  GPR_ASSERT(3 == intrusive_hash_map_size(&hash_map));

  // Test find.
  for (uint64_t i = 0; i < 3; ++i) {
    ptr_item *item = (ptr_item *)intrusive_hash_map_find(&hash_map, i);
    GPR_ASSERT(item != NULL);
    GPR_ASSERT(item->IHM_key == i && item->obj->val == i);
  }

  intrusive_hash_map_clear(&hash_map, &free_ptr_item);
  GPR_ASSERT(intrusive_hash_map_empty(&hash_map));
  intrusive_hash_map_free(&hash_map, &free_ptr_item);
}

// Check that the hash_map contains every key between [min_value, max_value]
// (inclusive).
void check_hash_map_values(intrusive_hash_map *hash_map, uint64_t min_value,
                           uint64_t max_value) {
  GPR_ASSERT(intrusive_hash_map_size(hash_map) == max_value - min_value + 1);

  for (uint64_t i = min_value; i <= max_value; ++i) {
    ptr_item *item = (ptr_item *)intrusive_hash_map_find(hash_map, i);
    GPR_ASSERT(item != NULL);
    GPR_ASSERT(item->obj->val == i);
  }
}

// Add many items and cause the hash_map to extend.
void test_extend() {
  intrusive_hash_map hash_map;
  intrusive_hash_map_init(&hash_map, kInitialLog2Size);

  const uint64_t kNumValues = (1 << 16);

  for (uint64_t i = 0; i < kNumValues; ++i) {
    ptr_item *item = make_ptr_item(i, i);
    bool ok = intrusive_hash_map_insert(&hash_map, (hm_item *)item);
    GPR_ASSERT(ok);
    if (i % 1000 == 0) {
      check_hash_map_values(&hash_map, 0, i);
    }
  }

  for (uint64_t i = 0; i < kNumValues; ++i) {
    ptr_item *item = (ptr_item *)intrusive_hash_map_find(&hash_map, i);
    GPR_ASSERT(item != NULL);
    GPR_ASSERT(item->IHM_key == i && item->obj->val == i);
    ptr_item *item2 = (ptr_item *)intrusive_hash_map_erase(&hash_map, i);
    GPR_ASSERT(item == item2);
    gpr_free(item->obj);
    gpr_free(item);
  }

  GPR_ASSERT(intrusive_hash_map_empty(&hash_map));
  intrusive_hash_map_free(&hash_map, &free_ptr_item);
}

void test_stress() {
  intrusive_hash_map hash_map;
  intrusive_hash_map_init(&hash_map, kInitialLog2Size);
  size_t n = 0;

  // Randomly add and insert entries 1000000 times.
  for (uint64_t i = 0; i < 1000000; ++i) {
    int op = rand() & 0x1;

    switch (op) {
      // Case 0 is insertion of entry.
      case 0: {
        uint64_t key = (uint64_t)(rand() % 10000);
        ptr_item *item = make_ptr_item(key, key);
        bool ok = intrusive_hash_map_insert(&hash_map, (hm_item *)item);
        if (ok) {
          n++;
        } else {
          gpr_free(item->obj);
          gpr_free(item);
        }
        break;
      }
      // Case 1 is removal of entry.
      case 1: {
        uint64_t key = (uint64_t)(rand() % 10000);
        ptr_item *item = (ptr_item *)intrusive_hash_map_find(&hash_map, key);
        if (item != NULL) {
          n--;
          GPR_ASSERT(key == item->obj->val);
          ptr_item *item2 =
              (ptr_item *)intrusive_hash_map_erase(&hash_map, key);
          GPR_ASSERT(item == item2);
          gpr_free(item->obj);
          gpr_free(item);
        }
        break;
      }
    }
  }
  // Check size
  GPR_ASSERT(n == intrusive_hash_map_size(&hash_map));

  // Clean the hash_map up.
  intrusive_hash_map_clear(&hash_map, &free_ptr_item);
  GPR_ASSERT(intrusive_hash_map_empty(&hash_map));
  intrusive_hash_map_free(&hash_map, &free_ptr_item);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  gpr_time_init();
  srand((unsigned)gpr_now(GPR_CLOCK_REALTIME).tv_nsec);

  test_empty();
  test_single_item();
  test_two_items();
  test_reset_clear();
  test_extend();
  test_stress();

  return 0;
}
