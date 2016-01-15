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
/*
- comment about key/value ptrs being to mem
- add comment about encode/decode being for RPC use only.
*/

#include <grpc/census.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include <stdbool.h>
#include <string.h>
#include "src/core/support/string.h"

// Functions in this file support the public tag_set API, as well as
// encoding/decoding tag_sets as part of context transmission across
// RPC's. The overall requirements (in approximate priority order) for the
// tag_set representations:
// 1. Efficient conversion to/from wire format
// 2. Minimal bytes used on-wire
// 3. Efficient tag set creation
// 4. Efficient lookup of value for a key
// 5. Efficient lookup of value for an index (to support iteration)
// 6. Minimal memory footprint
//
// Notes on tradeoffs/decisions:
// * tag includes 1 byte length of key, as well as nil-terminating byte. These
//   are to aid in efficient parsing and the ability to directly return key
//   strings. This is more important than saving a single byte/tag on the wire.
// * The wire encoding uses only single byte values. This eliminates the need
//   to handle endian-ness conversions.
// * Keep all tag information (keys/values/flags) in a single memory buffer,
//   that can be directly copied to the wire. This makes iteration by index
//   somewhat less efficient. If it becomes a problem, we could consider
//   building an index at tag_set creation.
// * Binary tags are encoded seperately from non-binary tags. There are a
//   primarily because non-binary tags are far more likely to be repeated
//   across multiple RPC calls, so are more efficiently cached and
//   compressed in any metadata schemes.
// * all lengths etc. are restricted to one byte. This eliminates endian
//   issues.

// Structure representing a set of tags. Essentially a count of number of tags
// present, and pointer to a chunk of memory that contains the per-tag details.
struct tag_set {
  int ntags;        // number of tags.
  int ntags_alloc;  // ntags + number of deleted tags (total number of tags
                    // in all of kvm). This will always be == ntags, except
                    // during the process of building a new tag set.
  size_t kvm_size;  // number of bytes allocated for key/value storage.
  size_t kvm_used;  // number of bytes of used key/value memory
  char *kvm;        // key/value memory. Consists of repeated entries of:
                    //   Offset  Size  Description
                    //     0      1    Key length, including trailing 0. (K)
                    //     1      1    Value length. (V)
                    //     2      1    Flags
                    //     3      K    Key bytes
                    //     3 + K  V    Value bytes
                    //
                    // We refer to the first 3 entries as the 'tag header'.
};

// Number of bytes in tag header.
#define TAG_HEADER_SIZE 3  // key length (1) + value length (1) + flags (1)
// Offsets to tag header entries.
#define KEY_LEN_OFFSET 0
#define VALUE_LEN_OFFSET 1
#define FLAG_OFFSET 2

// raw_tag represents the raw-storage form of a tag in the kvm of a tag_set.
struct raw_tag {
  uint8_t key_len;
  uint8_t value_len;
  uint8_t flags;
  char *key;
  char *value;
};

// use reserved flag bit for indication of deleted tag.
#define CENSUS_TAG_DELETED CENSUS_TAG_RESERVED
#define CENSUS_TAG_IS_DELETED(flags) (flags & CENSUS_TAG_DELETED)

// Primary (external) representation of a tag set. Composed of 3 underlying
// tag_set structs, one for each of the binary/printable propagated tags, and
// one for everything else.
struct census_tag_set {
  struct tag_set tags[3];
};

// Indices into the tags member of census_tag_set
#define PROPAGATED_TAGS 0
#define PROPAGATED_BINARY_TAGS 1
#define LOCAL_TAGS 2

// Extract a raw tag given a pointer (raw) to the tag header. Allow for some
// extra bytes in the tag header (see encode/decode for usage: allows for
// future expansion of the tag header).
static char *decode_tag(struct raw_tag *tag, char *header, int offset) {
  tag->key_len = (uint8_t)(*header++);
  tag->value_len = (uint8_t)(*header++);
  tag->flags = (uint8_t)(*header++);
  header += offset;
  tag->key = header;
  header += tag->key_len;
  tag->value = header;
  return header + tag->value_len;
}

// Make a copy (in 'to') of an existing tag_set.
static void tag_set_copy(struct tag_set *to, const struct tag_set *from) {
  memcpy(to, from, sizeof(struct tag_set));
  to->kvm = gpr_malloc(to->kvm_size);
  memcpy(to->kvm, from->kvm, to->kvm_used);
}

// Delete a tag from a tag set, if it exists (returns true it it did).
static bool tag_set_delete_tag(struct tag_set *tags, const char *key,
                               size_t key_len) {
  char *kvp = tags->kvm;
  for (int i = 0; i < tags->ntags_alloc; i++) {
    uint8_t *flags = (uint8_t *)(kvp + FLAG_OFFSET);
    struct raw_tag tag;
    kvp = decode_tag(&tag, kvp, 0);
    if (CENSUS_TAG_IS_DELETED(tag.flags)) continue;
    if ((key_len == tag.key_len) && (memcmp(key, tag.key, key_len) == 0)) {
      *flags |= CENSUS_TAG_DELETED;
      tags->ntags--;
      return true;
    }
  }
  return false;
}

// Delete a tag from a tag set, return true if it existed.
static bool cts_delete_tag(census_tag_set *tags, const census_tag *tag,
                           size_t key_len) {
  return (tag_set_delete_tag(&tags->tags[LOCAL_TAGS], tag->key, key_len) ||
          tag_set_delete_tag(&tags->tags[PROPAGATED_TAGS], tag->key, key_len) ||
          tag_set_delete_tag(&tags->tags[PROPAGATED_BINARY_TAGS], tag->key,
                             key_len));
}

// Add a tag to a tag set. Return true on sucess, false if the tag could
// not be added because of tag size constraints.
static bool tag_set_add_tag(struct tag_set *tags, const census_tag *tag,
                            size_t key_len) {
  if (tags->ntags == CENSUS_MAX_PROPAGATED_TAGS) {
    return false;
  }
  const size_t tag_size = key_len + tag->value_len + TAG_HEADER_SIZE;
  if (tags->kvm_used + tag_size > tags->kvm_size) {
    // allocate new memory if needed
    tags->kvm_size += 2 * CENSUS_MAX_TAG_KV_LEN + TAG_HEADER_SIZE;
    char *new_kvm = gpr_malloc(tags->kvm_size);
    memcpy(new_kvm, tags->kvm, tags->kvm_used);
    gpr_free(tags->kvm);
    tags->kvm = new_kvm;
  }
  char *kvp = tags->kvm + tags->kvm_used;
  *kvp++ = (char)key_len;
  *kvp++ = (char)tag->value_len;
  // ensure reserved flags are not used.
  *kvp++ = (char)(tag->flags & (CENSUS_TAG_PROPAGATE | CENSUS_TAG_STATS |
                                CENSUS_TAG_BINARY));
  memcpy(kvp, tag->key, key_len);
  kvp += key_len;
  memcpy(kvp, tag->value, tag->value_len);
  tags->kvm_used += tag_size;
  tags->ntags++;
  tags->ntags_alloc++;
  return true;
}

// Add a tag to a census_tag_set.
static void cts_add_tag(census_tag_set *tags, const census_tag *tag,
                        size_t key_len, census_tag_set_create_status *status) {
  // first delete the tag if it is already present
  bool deleted = cts_delete_tag(tags, tag, key_len);
  bool call_add = tag->value != NULL && tag->value_len != 0;
  bool added = false;
  if (call_add) {
    if (CENSUS_TAG_IS_PROPAGATED(tag->flags)) {
      if (CENSUS_TAG_IS_BINARY(tag->flags)) {
        added =
            tag_set_add_tag(&tags->tags[PROPAGATED_BINARY_TAGS], tag, key_len);
      } else {
        added = tag_set_add_tag(&tags->tags[PROPAGATED_TAGS], tag, key_len);
      }
    } else {
      added = tag_set_add_tag(&tags->tags[LOCAL_TAGS], tag, key_len);
    }
  }
  if (status) {
    if (deleted) {
      if (call_add) {
        status->n_modified_tags++;
      } else {
        status->n_deleted_tags++;
      }
    } else {
      if (added) {
        status->n_added_tags++;
      } else {
        status->n_ignored_tags++;
      }
    }
  }
}

// Remove any deleted tags from the tag set. Basic algorithm:
// 1) Walk through tag set to find first deleted tag. Record where it is.
// 2) Find the next not-deleted tag. Copy all of kvm from there to the end
//    "over" the deleted tags
// 3) repeat #1 and #2 until we have seen all tags
// 4) if we are still looking for a not-deleted tag, then all the end portion
//    of the kvm is deleted. Just reduce the used amount of memory by the
//    appropriate amount.
static void tag_set_flatten(struct tag_set *tags) {
  if (tags->ntags == tags->ntags_alloc) return;
  bool find_deleted = true;  // are we looking for deleted tags?
  char *kvp = tags->kvm;
  char *dbase;  // record location of deleted tag
  for (int i = 0; i < tags->ntags_alloc; i++) {
    struct raw_tag tag;
    char *next_kvp = decode_tag(&tag, kvp, 0);
    if (find_deleted) {
      if (CENSUS_TAG_IS_DELETED(tag.flags)) {
        dbase = kvp;
        find_deleted = false;
      }
    } else {
      if (!CENSUS_TAG_IS_DELETED(tag.flags)) {
        ptrdiff_t reduce = kvp - dbase;  // #bytes in deleted tags
        GPR_ASSERT(reduce > 0);
        ptrdiff_t copy_size = tags->kvm + tags->kvm_used - kvp;
        GPR_ASSERT(copy_size > 0);
        memmove(dbase, kvp, (size_t)copy_size);
        tags->kvm_used -= (size_t)reduce;
        next_kvp -= reduce;
        find_deleted = true;
      }
    }
    kvp = next_kvp;
  }
  if (!find_deleted) {
    GPR_ASSERT(dbase > tags->kvm);
    tags->kvm_used = (size_t)(dbase - tags->kvm);
  }
  tags->ntags_alloc = tags->ntags;
}

census_tag_set *census_tag_set_create(const census_tag_set *base,
                                      const census_tag *tags, int ntags,
                                      census_tag_set_create_status *status) {
  int n_invalid_tags = 0;
  if (status) {
    memset(status, 0, sizeof(*status));
  }
  census_tag_set *new_ts = gpr_malloc(sizeof(census_tag_set));
  if (base == NULL) {
    memset(new_ts, 0, sizeof(census_tag_set));
  } else {
    tag_set_copy(&new_ts->tags[PROPAGATED_TAGS], &base->tags[PROPAGATED_TAGS]);
    tag_set_copy(&new_ts->tags[PROPAGATED_BINARY_TAGS],
                 &base->tags[PROPAGATED_BINARY_TAGS]);
    tag_set_copy(&new_ts->tags[LOCAL_TAGS], &base->tags[LOCAL_TAGS]);
  }
  for (int i = 0; i < ntags; i++) {
    const census_tag *tag = &tags[i];
    size_t key_len = strlen(tag->key) + 1;
    // ignore the tag if it is too long/short.
    if (key_len != 1 && key_len <= CENSUS_MAX_TAG_KV_LEN &&
        tag->value_len <= CENSUS_MAX_TAG_KV_LEN) {
      cts_add_tag(new_ts, tag, key_len, status);
    } else {
      n_invalid_tags++;
    }
  }
  tag_set_flatten(&new_ts->tags[PROPAGATED_TAGS]);
  tag_set_flatten(&new_ts->tags[PROPAGATED_BINARY_TAGS]);
  tag_set_flatten(&new_ts->tags[LOCAL_TAGS]);
  if (status != NULL) {
    status->n_propagated_tags = new_ts->tags[PROPAGATED_TAGS].ntags;
    status->n_propagated_binary_tags =
        new_ts->tags[PROPAGATED_BINARY_TAGS].ntags;
    status->n_local_tags = new_ts->tags[LOCAL_TAGS].ntags;
    status->n_invalid_tags = n_invalid_tags;
  }
  return new_ts;
}

void census_tag_set_destroy(census_tag_set *tags) {
  gpr_free(tags->tags[PROPAGATED_TAGS].kvm);
  gpr_free(tags->tags[PROPAGATED_BINARY_TAGS].kvm);
  gpr_free(tags->tags[LOCAL_TAGS].kvm);
  gpr_free(tags);
}

int census_tag_set_ntags(const census_tag_set *tags) {
  return tags->tags[PROPAGATED_TAGS].ntags +
         tags->tags[PROPAGATED_BINARY_TAGS].ntags +
         tags->tags[LOCAL_TAGS].ntags;
}

/* Initialize a tag set iterator. Must be called before first use of the
   iterator. */
void census_tag_set_initialize_iterator(const census_tag_set *tags,
                                        census_tag_set_iterator *iterator) {
  iterator->tags = tags;
  iterator->index = 0;
  if (tags->tags[PROPAGATED_TAGS].ntags != 0) {
    iterator->base = PROPAGATED_TAGS;
    iterator->kvm = tags->tags[PROPAGATED_TAGS].kvm;
  } else if (tags->tags[PROPAGATED_BINARY_TAGS].ntags != 0) {
    iterator->base = PROPAGATED_BINARY_TAGS;
    iterator->kvm = tags->tags[PROPAGATED_BINARY_TAGS].kvm;
  } else if (tags->tags[LOCAL_TAGS].ntags != 0) {
    iterator->base = LOCAL_TAGS;
    iterator->kvm = tags->tags[LOCAL_TAGS].kvm;
  } else {
    iterator->base = -1;
  }
}

/* Get the contents of the "next" tag in the tag set. If there are no more
   tags in the tag set, returns 0 (and 'tag' contents will be unchanged),
   otherwise returns 1. */
int census_tag_set_next_tag(census_tag_set_iterator *iterator,
                            census_tag *tag) {
  if (iterator->base < 0) {
    return 0;
  }
  struct raw_tag raw;
  iterator->kvm = decode_tag(&raw, iterator->kvm, 0);
  tag->key = raw.key;
  tag->value = raw.value;
  tag->value_len = raw.value_len;
  tag->flags = raw.flags;
  if (++iterator->index == iterator->tags->tags[iterator->base].ntags) {
    do {
      if (iterator->base == LOCAL_TAGS) {
        iterator->base = -1;
        return 1;
      }
    } while (iterator->tags->tags[++iterator->base].ntags == 0);
    iterator->index = 0;
    iterator->kvm = iterator->tags->tags[iterator->base].kvm;
  }
  return 1;
}

// Find a tag in a tag_set by key. Return true if found, false otherwise.
static bool tag_set_get_tag_by_key(const struct tag_set *tags, const char *key,
                                   size_t key_len, census_tag *tag) {
  char *kvp = tags->kvm;
  for (int i = 0; i < tags->ntags; i++) {
    struct raw_tag raw;
    kvp = decode_tag(&raw, kvp, 0);
    if (key_len == raw.key_len && memcmp(raw.key, key, key_len) == 0) {
      tag->key = raw.key;
      tag->value = raw.value;
      tag->value_len = raw.value_len;
      tag->flags = raw.flags;
      return true;
    }
  }
  return false;
}

int census_tag_set_get_tag_by_key(const census_tag_set *tags, const char *key,
                                  census_tag *tag) {
  size_t key_len = strlen(key) + 1;
  if (key_len == 1) {
    return 0;
  }
  if (tag_set_get_tag_by_key(&tags->tags[PROPAGATED_TAGS], key, key_len, tag) ||
      tag_set_get_tag_by_key(&tags->tags[PROPAGATED_BINARY_TAGS], key, key_len,
                             tag) ||
      tag_set_get_tag_by_key(&tags->tags[LOCAL_TAGS], key, key_len, tag)) {
    return 1;
  }
  return 0;
}

// tag_set encoding and decoding functions.
//
// Wire format for tag sets on the wire:
//
// First, a tag set header:
//
// offset   bytes  description
//   0        1    version number
//   1        1    number of bytes in this header. This allows for future
//                 expansion.
//   2        1    number of bytes in each tag header.
//   3        1    ntags value from tag set.
//
//   This is followed by the key/value memory from struct tag_set.

#define ENCODED_VERSION 0      // Version number
#define ENCODED_HEADER_SIZE 4  // size of tag set header

// Encode a tag set. Returns 0 if buffer is too small.
static size_t tag_set_encode(const struct tag_set *tags, char *buffer,
                             size_t buf_size) {
  if (buf_size < ENCODED_HEADER_SIZE + tags->kvm_used) {
    return 0;
  }
  buf_size -= ENCODED_HEADER_SIZE;
  *buffer++ = (char)ENCODED_VERSION;
  *buffer++ = (char)ENCODED_HEADER_SIZE;
  *buffer++ = (char)TAG_HEADER_SIZE;
  *buffer++ = (char)tags->ntags;
  if (tags->ntags == 0) {
    return ENCODED_HEADER_SIZE;
  }
  memcpy(buffer, tags->kvm, tags->kvm_used);
  return ENCODED_HEADER_SIZE + tags->kvm_used;
}

size_t census_tag_set_encode_propagated(const census_tag_set *tags,
                                        char *buffer, size_t buf_size) {
  return tag_set_encode(&tags->tags[PROPAGATED_TAGS], buffer, buf_size);
}

size_t census_tag_set_encode_propagated_binary(const census_tag_set *tags,
                                               char *buffer, size_t buf_size) {
  return tag_set_encode(&tags->tags[PROPAGATED_BINARY_TAGS], buffer, buf_size);
}

// Decode a tag set.
static void tag_set_decode(struct tag_set *tags, const char *buffer,
                           size_t size) {
  uint8_t version = (uint8_t)(*buffer++);
  uint8_t header_size = (uint8_t)(*buffer++);
  uint8_t tag_header_size = (uint8_t)(*buffer++);
  tags->ntags = tags->ntags_alloc = (int)(*buffer++);
  if (tags->ntags == 0) {
    tags->ntags_alloc = 0;
    tags->kvm_size = 0;
    tags->kvm_used = 0;
    tags->kvm = NULL;
    return;
  }
  if (header_size != ENCODED_HEADER_SIZE) {
    GPR_ASSERT(version != ENCODED_VERSION);
    GPR_ASSERT(ENCODED_HEADER_SIZE < header_size);
    buffer += (header_size - ENCODED_HEADER_SIZE);
  }
  tags->kvm_used = size - header_size;
  tags->kvm_size = tags->kvm_used + CENSUS_MAX_TAG_KV_LEN;
  tags->kvm = gpr_malloc(tags->kvm_size);
  if (tag_header_size != TAG_HEADER_SIZE) {
    // something new in the tag information. I don't understand it, so
    // don't copy it over.
    GPR_ASSERT(version != ENCODED_VERSION);
    GPR_ASSERT(tag_header_size > TAG_HEADER_SIZE);
    char *kvp = tags->kvm;
    for (int i = 0; i < tags->ntags; i++) {
      memcpy(kvp, buffer, TAG_HEADER_SIZE);
      kvp += header_size;
      struct raw_tag raw;
      buffer =
          decode_tag(&raw, (char *)buffer, tag_header_size - TAG_HEADER_SIZE);
      memcpy(kvp, raw.key, (size_t)raw.key_len + raw.value_len);
      kvp += raw.key_len + raw.value_len;
    }
  } else {
    memcpy(tags->kvm, buffer, tags->kvm_used);
  }
}

census_tag_set *census_tag_set_decode(const char *buffer, size_t size,
                                      const char *bin_buffer, size_t bin_size,
                                      census_tag_set_create_status *status) {
  if (status) {
    memset(status, 0, sizeof(*status));
  }
  census_tag_set *new_ts = gpr_malloc(sizeof(census_tag_set));
  memset(&new_ts->tags[LOCAL_TAGS], 0, sizeof(struct tag_set));
  if (buffer == NULL) {
    memset(&new_ts->tags[PROPAGATED_TAGS], 0, sizeof(struct tag_set));
  } else {
    tag_set_decode(&new_ts->tags[PROPAGATED_TAGS], buffer, size);
  }
  if (bin_buffer == NULL) {
    memset(&new_ts->tags[PROPAGATED_BINARY_TAGS], 0, sizeof(struct tag_set));
  } else {
    tag_set_decode(&new_ts->tags[PROPAGATED_BINARY_TAGS], bin_buffer, bin_size);
  }
  if (status) {
    status->n_propagated_tags = new_ts->tags[PROPAGATED_TAGS].ntags;
    status->n_propagated_binary_tags =
        new_ts->tags[PROPAGATED_BINARY_TAGS].ntags;
  }
  // TODO(aveitch): check that BINARY flag is correct for each type.
  return new_ts;
}
