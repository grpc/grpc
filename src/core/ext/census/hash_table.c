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

#include "src/core/ext/census/hash_table.h"

#include <stddef.h>
#include <stdio.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#define CENSUS_HT_NUM_BUCKETS 1999

/* A single hash table data entry */
typedef struct ht_entry {
  census_ht_key key;
  void *data;
  struct ht_entry *next;
} ht_entry;

/* hash table bucket */
typedef struct bucket {
  /* NULL if bucket is empty */
  ht_entry *next;
  /* -1 if all buckets are empty. */
  int32_t prev_non_empty_bucket;
  /* -1 if all buckets are empty. */
  int32_t next_non_empty_bucket;
} bucket;

struct unresizable_hash_table {
  /* Number of entries in the table */
  size_t size;
  /* Number of buckets */
  uint32_t num_buckets;
  /* Array of buckets initialized at creation time. Memory consumption is
     16 bytes per bucket on a 64-bit platform. */
  bucket *buckets;
  /* Index of the first non-empty bucket. -1 iff size == 0. */
  int32_t first_non_empty_bucket;
  /* Index of the last non_empty bucket. -1 iff size == 0. */
  int32_t last_non_empty_bucket;
  /* Immutable options of this hash table, initialized at creation time. */
  census_ht_option options;
};

typedef struct entry_locator {
  int32_t bucket_idx;
  int is_first_in_chain;
  int found;
  ht_entry *prev_entry;
} entry_locator;

/* Asserts if option is not valid. */
void check_options(const census_ht_option *option) {
  GPR_ASSERT(option != NULL);
  GPR_ASSERT(option->num_buckets > 0);
  GPR_ASSERT(option->key_type == CENSUS_HT_UINT64 ||
             option->key_type == CENSUS_HT_POINTER);
  if (option->key_type == CENSUS_HT_UINT64) {
    GPR_ASSERT(option->hash == NULL);
  } else if (option->key_type == CENSUS_HT_POINTER) {
    GPR_ASSERT(option->hash != NULL);
    GPR_ASSERT(option->compare_keys != NULL);
  }
}

#define REMOVE_NEXT(options, ptr) \
  do {                            \
    ht_entry *tmp = (ptr)->next;  \
    (ptr)->next = tmp->next;      \
    delete_entry(options, tmp);   \
  } while (0)

static void delete_entry(const census_ht_option *opt, ht_entry *p) {
  if (opt->delete_data != NULL) {
    opt->delete_data(p->data);
  }
  if (opt->delete_key != NULL) {
    opt->delete_key(p->key.ptr);
  }
  gpr_free(p);
}

static uint64_t hash(const census_ht_option *opt, census_ht_key key) {
  return opt->key_type == CENSUS_HT_UINT64 ? key.val : opt->hash(key.ptr);
}

census_ht *census_ht_create(const census_ht_option *option) {
  int i;
  census_ht *ret = NULL;
  check_options(option);
  ret = (census_ht *)gpr_malloc(sizeof(census_ht));
  ret->size = 0;
  ret->num_buckets = option->num_buckets;
  ret->buckets = (bucket *)gpr_malloc(sizeof(bucket) * ret->num_buckets);
  ret->options = *option;
  /* initialize each bucket */
  for (i = 0; i < ret->options.num_buckets; i++) {
    ret->buckets[i].prev_non_empty_bucket = -1;
    ret->buckets[i].next_non_empty_bucket = -1;
    ret->buckets[i].next = NULL;
  }
  return ret;
}

static int32_t find_bucket_idx(const census_ht *ht, census_ht_key key) {
  return hash(&ht->options, key) % ht->num_buckets;
}

static int keys_match(const census_ht_option *opt, const ht_entry *p,
                      const census_ht_key key) {
  GPR_ASSERT(opt->key_type == CENSUS_HT_UINT64 ||
             opt->key_type == CENSUS_HT_POINTER);
  if (opt->key_type == CENSUS_HT_UINT64) return p->key.val == key.val;
  return !opt->compare_keys((p->key).ptr, key.ptr);
}

static entry_locator ht_find(const census_ht *ht, census_ht_key key) {
  entry_locator loc = {0, 0, 0, NULL};
  int32_t idx = 0;
  ht_entry *ptr = NULL;
  GPR_ASSERT(ht != NULL);
  idx = find_bucket_idx(ht, key);
  ptr = ht->buckets[idx].next;
  if (ptr == NULL) {
    /* bucket is empty */
    return loc;
  }
  if (keys_match(&ht->options, ptr, key)) {
    loc.bucket_idx = idx;
    loc.is_first_in_chain = 1;
    loc.found = 1;
    return loc;
  } else {
    for (; ptr->next != NULL; ptr = ptr->next) {
      if (keys_match(&ht->options, ptr->next, key)) {
        loc.bucket_idx = idx;
        loc.is_first_in_chain = 0;
        loc.found = 1;
        loc.prev_entry = ptr;
        return loc;
      }
    }
  }
  /* Could not find the key */
  return loc;
}

void *census_ht_find(const census_ht *ht, census_ht_key key) {
  entry_locator loc = ht_find(ht, key);
  if (loc.found == 0) {
    return NULL;
  }
  return loc.is_first_in_chain ? ht->buckets[loc.bucket_idx].next->data
                               : loc.prev_entry->next->data;
}

void census_ht_insert(census_ht *ht, census_ht_key key, void *data) {
  int32_t idx = find_bucket_idx(ht, key);
  ht_entry *ptr = NULL;
  entry_locator loc = ht_find(ht, key);
  if (loc.found) {
    /* Replace old value with new value. */
    ptr = loc.is_first_in_chain ? ht->buckets[loc.bucket_idx].next
                                : loc.prev_entry->next;
    if (ht->options.delete_data != NULL) {
      ht->options.delete_data(ptr->data);
    }
    ptr->data = data;
    return;
  }

  /* first entry in the table. */
  if (ht->size == 0) {
    ht->buckets[idx].next_non_empty_bucket = -1;
    ht->buckets[idx].prev_non_empty_bucket = -1;
    ht->first_non_empty_bucket = idx;
    ht->last_non_empty_bucket = idx;
  } else if (ht->buckets[idx].next == NULL) {
    /* first entry in the bucket. */
    ht->buckets[ht->last_non_empty_bucket].next_non_empty_bucket = idx;
    ht->buckets[idx].prev_non_empty_bucket = ht->last_non_empty_bucket;
    ht->buckets[idx].next_non_empty_bucket = -1;
    ht->last_non_empty_bucket = idx;
  }
  ptr = (ht_entry *)gpr_malloc(sizeof(ht_entry));
  ptr->key = key;
  ptr->data = data;
  ptr->next = ht->buckets[idx].next;
  ht->buckets[idx].next = ptr;
  ht->size++;
}

void census_ht_erase(census_ht *ht, census_ht_key key) {
  entry_locator loc = ht_find(ht, key);
  if (loc.found == 0) {
    /* noop if not found */
    return;
  }
  ht->size--;
  if (loc.is_first_in_chain) {
    bucket *b = &ht->buckets[loc.bucket_idx];
    GPR_ASSERT(b->next != NULL);
    /* The only entry in the bucket */
    if (b->next->next == NULL) {
      int prev = b->prev_non_empty_bucket;
      int next = b->next_non_empty_bucket;
      if (prev != -1) {
        ht->buckets[prev].next_non_empty_bucket = next;
      } else {
        ht->first_non_empty_bucket = next;
      }
      if (next != -1) {
        ht->buckets[next].prev_non_empty_bucket = prev;
      } else {
        ht->last_non_empty_bucket = prev;
      }
    }
    REMOVE_NEXT(&ht->options, b);
  } else {
    GPR_ASSERT(loc.prev_entry->next != NULL);
    REMOVE_NEXT(&ht->options, loc.prev_entry);
  }
}

/* Returns NULL if input table is empty. */
census_ht_kv *census_ht_get_all_elements(const census_ht *ht, size_t *num) {
  census_ht_kv *ret = NULL;
  int i = 0;
  int32_t idx = -1;
  GPR_ASSERT(ht != NULL && num != NULL);
  *num = ht->size;
  if (*num == 0) {
    return NULL;
  }

  ret = (census_ht_kv *)gpr_malloc(sizeof(census_ht_kv) * ht->size);
  idx = ht->first_non_empty_bucket;
  while (idx >= 0) {
    ht_entry *ptr = ht->buckets[idx].next;
    for (; ptr != NULL; ptr = ptr->next) {
      ret[i].k = ptr->key;
      ret[i].v = ptr->data;
      i++;
    }
    idx = ht->buckets[idx].next_non_empty_bucket;
  }
  return ret;
}

static void ht_delete_entry_chain(const census_ht_option *options,
                                  ht_entry *first) {
  if (first == NULL) {
    return;
  }
  if (first->next != NULL) {
    ht_delete_entry_chain(options, first->next);
  }
  delete_entry(options, first);
}

void census_ht_destroy(census_ht *ht) {
  unsigned i;
  for (i = 0; i < ht->num_buckets; ++i) {
    ht_delete_entry_chain(&ht->options, ht->buckets[i].next);
  }
  gpr_free(ht->buckets);
  gpr_free(ht);
}

size_t census_ht_get_size(const census_ht *ht) { return ht->size; }
