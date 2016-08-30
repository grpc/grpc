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

#ifndef GRPC_CORE_EXT_CENSUS_HASH_TABLE_H
#define GRPC_CORE_EXT_CENSUS_HASH_TABLE_H

#include <stddef.h>

#include <grpc/support/port_platform.h>

/* A chain based hash table with fixed number of buckets.
   Your probably shouldn't use this code directly. It is implemented for the
   use case in census trace store and stats store, where number of entries in
   the table is in the scale of upto several thousands, entries are added and
   removed from the table very frequently (~100k/s), the frequency of find()
   operations is roughly several times of the frequency of insert() and erase()
   Comparing to find(), the insert(), erase() and get_all_entries() operations
   are much less freqent (<1/s).

   Per bucket memory overhead is about (8 + sizeof(intptr_t) bytes.
   Per entry memory overhead is about (8 + 2 * sizeof(intptr_t) bytes.

   All functions are not thread-safe. Synchronization will be provided in the
   upper layer (in trace store and stats store).
*/

/* Opaque hash table struct */
typedef struct unresizable_hash_table census_ht;

/* Currently, the hash_table can take two types of keys. (uint64 for trace
   store and const char* for stats store). */
typedef union {
  uint64_t val;
  void *ptr;
} census_ht_key;

typedef enum census_ht_key_type {
  CENSUS_HT_UINT64 = 0,
  CENSUS_HT_POINTER = 1
} census_ht_key_type;

typedef struct census_ht_option {
  /* Type of hash key */
  census_ht_key_type key_type;
  /* Desired number of buckets, preferably a prime number */
  int32_t num_buckets;
  /* Fucntion to calculate uint64 hash value of the key. Only takes effect if
     key_type is POINTER. */
  uint64_t (*hash)(const void *);
  /* Function to compare two keys, returns 0 iff equal. Only takes effect if
     key_type is POINTER */
  int (*compare_keys)(const void *k1, const void *k2);
  /* Value deleter. NULL if no specialized delete function is needed. */
  void (*delete_data)(void *);
  /* Key deleter. NULL if table does not own the key. (e.g. key is part of the
     value or key is not owned by the table.) */
  void (*delete_key)(void *);
} census_ht_option;

/* Creates a hashtable with fixed number of buckets according to the settings
   specified in 'options' arg. Function pointers "hash" and "compare_keys" must
   be provided if key_type is POINTER. Asserts if fail to create. */
census_ht *census_ht_create(const census_ht_option *options);

/* Deletes hash table instance. Frees all dynamic memory owned by ht.*/
void census_ht_destroy(census_ht *ht);

/* Inserts the input key-val pair into hash_table. If an entry with the same key
   exists in the table, the corresponding value will be overwritten by the input
   val. */
void census_ht_insert(census_ht *ht, census_ht_key key, void *val);

/* Returns pointer to data, returns NULL if not found. */
void *census_ht_find(const census_ht *ht, census_ht_key key);

/* Erase hash table entry with input key. Noop if key is not found. */
void census_ht_erase(census_ht *ht, census_ht_key key);

typedef struct census_ht_kv {
  census_ht_key k;
  void *v;
} census_ht_kv;

/* Returns an array of pointers to all values in the hash table. Order of the
   elements can be arbitrary. Sets 'num' to the size of returned array. Caller
   owns returned array. */
census_ht_kv *census_ht_get_all_elements(const census_ht *ht, size_t *num);

/* Returns number of elements kept. */
size_t census_ht_get_size(const census_ht *ht);

/* Functor applied on each key-value pair while iterating through entries in the
   table. The functor should not mutate data. */
typedef void (*census_ht_itr_cb)(census_ht_key key, const void *val_ptr,
                                 void *state);

/* Iterates through all key-value pairs in the hash_table. The callback function
   should not invalidate data entries. */
uint64_t census_ht_for_all(const census_ht *ht, census_ht_itr_cb);

#endif /* GRPC_CORE_EXT_CENSUS_HASH_TABLE_H */
