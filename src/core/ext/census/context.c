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

#include <grpc/census.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include <stdbool.h>
#include <string.h>
#include "src/core/lib/support/string.h"

// Functions in this file support the public context API, including
// encoding/decoding as part of context propagation across RPC's. The overall
// requirements (in approximate priority order) for the
// context representation:
// 1. Efficient conversion to/from wire format
// 2. Minimal bytes used on-wire
// 3. Efficient context creation
// 4. Efficient lookup of tag value for a key
// 5. Efficient iteration over tags
// 6. Minimal memory footprint
//
// Notes on tradeoffs/decisions:
// * tag includes 1 byte length of key, as well as nil-terminating byte. These
//   are to aid in efficient parsing and the ability to directly return key
//   strings. This is more important than saving a single byte/tag on the wire.
// * The wire encoding uses only single byte values. This eliminates the need
//   to handle endian-ness conversions. It also means there is a hard upper
//   limit of 255 for both CENSUS_MAX_TAG_KV_LEN and CENSUS_MAX_PROPAGATED_TAGS.
// * Keep all tag information (keys/values/flags) in a single memory buffer,
//   that can be directly copied to the wire.

// min and max valid chars in tag keys and values. All printable ASCII is OK.
#define MIN_VALID_TAG_CHAR 32   // ' '
#define MAX_VALID_TAG_CHAR 126  // '~'

// Structure representing a set of tags. Essentially a count of number of tags
// present, and pointer to a chunk of memory that contains the per-tag details.
struct tag_set {
  int ntags;        // number of tags.
  int ntags_alloc;  // ntags + number of deleted tags (total number of tags
  // in all of kvm). This will always be == ntags, except during the process
  // of building a new tag set.
  size_t kvm_size;  // number of bytes allocated for key/value storage.
  size_t kvm_used;  // number of bytes of used key/value memory
  char *kvm;        // key/value memory. Consists of repeated entries of:
  //   Offset  Size  Description
  //     0      1    Key length, including trailing 0. (K)
  //     1      1    Value length, including trailing 0 (V)
  //     2      1    Flags
  //     3      K    Key bytes
  //     3 + K  V    Value bytes
  //
  // We refer to the first 3 entries as the 'tag header'. If extra values are
  // introduced in the header, you will need to modify the TAG_HEADER_SIZE
  // constant, the raw_tag structure (and everything that uses it) and the
  // encode/decode functions appropriately.
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

// Use a reserved flag bit for indication of deleted tag.
#define CENSUS_TAG_DELETED CENSUS_TAG_RESERVED
#define CENSUS_TAG_IS_DELETED(flags) (flags & CENSUS_TAG_DELETED)

// Primary representation of a context. Composed of 2 underlying tag_set
// structs, one each for propagated and local (non-propagated) tags. This is
// to efficiently support tag encoding/decoding.
// TODO(aveitch): need to add tracing id's/structure.
struct census_context {
  struct tag_set tags[2];
  census_context_status status;
};

// Indices into the tags member of census_context
#define PROPAGATED_TAGS 0
#define LOCAL_TAGS 1

// Validate (check all characters are in range and size is less than limit) a
// key or value string. Returns 0 if the string is invalid, or the length
// (including terminator) if valid.
static size_t validate_tag(const char *kv) {
  size_t len = 1;
  char ch;
  while ((ch = *kv++) != 0) {
    if (ch < MIN_VALID_TAG_CHAR || ch > MAX_VALID_TAG_CHAR) {
      return 0;
    }
    len++;
  }
  if (len > CENSUS_MAX_TAG_KV_LEN) {
    return 0;
  }
  return len;
}

// Extract a raw tag given a pointer (raw) to the tag header. Allow for some
// extra bytes in the tag header (see encode/decode functions for usage: this
// allows for future expansion of the tag header).
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
  memcpy(to->kvm, from->kvm, from->kvm_used);
}

// Delete a tag from a tag_set, if it exists (returns true if it did).
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

// Delete a tag from a context, return true if it existed.
static bool context_delete_tag(census_context *context, const census_tag *tag,
                               size_t key_len) {
  return (
      tag_set_delete_tag(&context->tags[LOCAL_TAGS], tag->key, key_len) ||
      tag_set_delete_tag(&context->tags[PROPAGATED_TAGS], tag->key, key_len));
}

// Add a tag to a tag_set. Return true on success, false if the tag could
// not be added because of constraints on tag set size. This function should
// not be called if the tag may already exist (in a non-deleted state) in
// the tag_set, as that would result in two tags with the same key.
static bool tag_set_add_tag(struct tag_set *tags, const census_tag *tag,
                            size_t key_len, size_t value_len) {
  if (tags->ntags == CENSUS_MAX_PROPAGATED_TAGS) {
    return false;
  }
  const size_t tag_size = key_len + value_len + TAG_HEADER_SIZE;
  if (tags->kvm_used + tag_size > tags->kvm_size) {
    // allocate new memory if needed
    tags->kvm_size += 2 * CENSUS_MAX_TAG_KV_LEN + TAG_HEADER_SIZE;
    char *new_kvm = gpr_malloc(tags->kvm_size);
    if (tags->kvm_used > 0) memcpy(new_kvm, tags->kvm, tags->kvm_used);
    gpr_free(tags->kvm);
    tags->kvm = new_kvm;
  }
  char *kvp = tags->kvm + tags->kvm_used;
  *kvp++ = (char)key_len;
  *kvp++ = (char)value_len;
  // ensure reserved flags are not used.
  *kvp++ = (char)(tag->flags & (CENSUS_TAG_PROPAGATE | CENSUS_TAG_STATS));
  memcpy(kvp, tag->key, key_len);
  kvp += key_len;
  memcpy(kvp, tag->value, value_len);
  tags->kvm_used += tag_size;
  tags->ntags++;
  tags->ntags_alloc++;
  return true;
}

// Add/modify/delete a tag to/in a context. Caller must validate that tag key
// etc. are valid.
static void context_modify_tag(census_context *context, const census_tag *tag,
                               size_t key_len, size_t value_len) {
  // First delete the tag if it is already present.
  bool deleted = context_delete_tag(context, tag, key_len);
  bool added = false;
  if (CENSUS_TAG_IS_PROPAGATED(tag->flags)) {
    added = tag_set_add_tag(&context->tags[PROPAGATED_TAGS], tag, key_len,
                            value_len);
  } else {
    added =
        tag_set_add_tag(&context->tags[LOCAL_TAGS], tag, key_len, value_len);
  }

  if (deleted) {
    context->status.n_modified_tags++;
  } else {
    if (added) {
      context->status.n_added_tags++;
    } else {
      context->status.n_ignored_tags++;
    }
  }
}

// Remove memory used for deleted tags from a tag set. Basic algorithm:
// 1) Walk through tag set to find first deleted tag. Record where it is.
// 2) Find the next not-deleted tag. Copy all of kvm from there to the end
//    "over" the deleted tags
// 3) repeat #1 and #2 until we have seen all tags
// 4) if we are still looking for a not-deleted tag, then all the end portion
//    of the kvm is deleted. Just reduce the used amount of memory by the
//    appropriate amount.
static void tag_set_flatten(struct tag_set *tags) {
  if (tags->ntags == tags->ntags_alloc) return;
  bool found_deleted = false;  // found a deleted tag.
  char *kvp = tags->kvm;
  char *dbase = NULL;  // record location of deleted tag
  for (int i = 0; i < tags->ntags_alloc; i++) {
    struct raw_tag tag;
    char *next_kvp = decode_tag(&tag, kvp, 0);
    if (found_deleted) {
      if (!CENSUS_TAG_IS_DELETED(tag.flags)) {
        ptrdiff_t reduce = kvp - dbase;  // #bytes in deleted tags
        GPR_ASSERT(reduce > 0);
        ptrdiff_t copy_size = tags->kvm + tags->kvm_used - kvp;
        GPR_ASSERT(copy_size > 0);
        memmove(dbase, kvp, (size_t)copy_size);
        tags->kvm_used -= (size_t)reduce;
        next_kvp -= reduce;
        found_deleted = false;
      }
    } else {
      if (CENSUS_TAG_IS_DELETED(tag.flags)) {
        dbase = kvp;
        found_deleted = true;
      }
    }
    kvp = next_kvp;
  }
  if (found_deleted) {
    GPR_ASSERT(dbase > tags->kvm);
    tags->kvm_used = (size_t)(dbase - tags->kvm);
  }
  tags->ntags_alloc = tags->ntags;
}

census_context *census_context_create(const census_context *base,
                                      const census_tag *tags, int ntags,
                                      census_context_status const **status) {
  census_context *context = gpr_malloc(sizeof(census_context));
  // If we are given a base, copy it into our new tag set. Otherwise set it
  // to zero/NULL everything.
  if (base == NULL) {
    memset(context, 0, sizeof(census_context));
  } else {
    tag_set_copy(&context->tags[PROPAGATED_TAGS], &base->tags[PROPAGATED_TAGS]);
    tag_set_copy(&context->tags[LOCAL_TAGS], &base->tags[LOCAL_TAGS]);
    memset(&context->status, 0, sizeof(context->status));
  }
  // Walk over the additional tags and, for those that aren't invalid, modify
  // the context to add/replace/delete as required.
  for (int i = 0; i < ntags; i++) {
    const census_tag *tag = &tags[i];
    size_t key_len = validate_tag(tag->key);
    // ignore the tag if it is invalid or too short.
    if (key_len <= 1) {
      context->status.n_invalid_tags++;
    } else {
      if (tag->value != NULL) {
        size_t value_len = validate_tag(tag->value);
        if (value_len != 0) {
          context_modify_tag(context, tag, key_len, value_len);
        } else {
          context->status.n_invalid_tags++;
        }
      } else {
        if (context_delete_tag(context, tag, key_len)) {
          context->status.n_deleted_tags++;
        }
      }
    }
  }
  // Remove any deleted tags, update status if needed, and return.
  tag_set_flatten(&context->tags[PROPAGATED_TAGS]);
  tag_set_flatten(&context->tags[LOCAL_TAGS]);
  context->status.n_propagated_tags = context->tags[PROPAGATED_TAGS].ntags;
  context->status.n_local_tags = context->tags[LOCAL_TAGS].ntags;
  if (status) {
    *status = &context->status;
  }
  return context;
}

const census_context_status *census_context_get_status(
    const census_context *context) {
  return &context->status;
}

void census_context_destroy(census_context *context) {
  gpr_free(context->tags[PROPAGATED_TAGS].kvm);
  gpr_free(context->tags[LOCAL_TAGS].kvm);
  gpr_free(context);
}

void census_context_initialize_iterator(const census_context *context,
                                        census_context_iterator *iterator) {
  iterator->context = context;
  iterator->index = 0;
  if (context->tags[PROPAGATED_TAGS].ntags != 0) {
    iterator->base = PROPAGATED_TAGS;
    iterator->kvm = context->tags[PROPAGATED_TAGS].kvm;
  } else if (context->tags[LOCAL_TAGS].ntags != 0) {
    iterator->base = LOCAL_TAGS;
    iterator->kvm = context->tags[LOCAL_TAGS].kvm;
  } else {
    iterator->base = -1;
  }
}

int census_context_next_tag(census_context_iterator *iterator,
                            census_tag *tag) {
  if (iterator->base < 0) {
    return 0;
  }
  struct raw_tag raw;
  iterator->kvm = decode_tag(&raw, iterator->kvm, 0);
  tag->key = raw.key;
  tag->value = raw.value;
  tag->flags = raw.flags;
  if (++iterator->index == iterator->context->tags[iterator->base].ntags) {
    do {
      if (iterator->base == LOCAL_TAGS) {
        iterator->base = -1;
        return 1;
      }
    } while (iterator->context->tags[++iterator->base].ntags == 0);
    iterator->index = 0;
    iterator->kvm = iterator->context->tags[iterator->base].kvm;
  }
  return 1;
}

// Find a tag in a tag_set by key. Return true if found, false otherwise.
static bool tag_set_get_tag(const struct tag_set *tags, const char *key,
                            size_t key_len, census_tag *tag) {
  char *kvp = tags->kvm;
  for (int i = 0; i < tags->ntags; i++) {
    struct raw_tag raw;
    kvp = decode_tag(&raw, kvp, 0);
    if (key_len == raw.key_len && memcmp(raw.key, key, key_len) == 0) {
      tag->key = raw.key;
      tag->value = raw.value;
      tag->flags = raw.flags;
      return true;
    }
  }
  return false;
}

int census_context_get_tag(const census_context *context, const char *key,
                           census_tag *tag) {
  size_t key_len = strlen(key) + 1;
  if (key_len == 1) {
    return 0;
  }
  if (tag_set_get_tag(&context->tags[PROPAGATED_TAGS], key, key_len, tag) ||
      tag_set_get_tag(&context->tags[LOCAL_TAGS], key, key_len, tag)) {
    return 1;
  }
  return 0;
}

// Context encoding and decoding functions.
//
// Wire format for tag_set's on the wire:
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

size_t census_context_encode(const census_context *context, char *buffer,
                             size_t buf_size) {
  return tag_set_encode(&context->tags[PROPAGATED_TAGS], buffer, buf_size);
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

census_context *census_context_decode(const char *buffer, size_t size) {
  census_context *context = gpr_malloc(sizeof(census_context));
  memset(&context->tags[LOCAL_TAGS], 0, sizeof(struct tag_set));
  if (buffer == NULL) {
    memset(&context->tags[PROPAGATED_TAGS], 0, sizeof(struct tag_set));
  } else {
    tag_set_decode(&context->tags[PROPAGATED_TAGS], buffer, size);
  }
  memset(&context->status, 0, sizeof(context->status));
  context->status.n_propagated_tags = context->tags[PROPAGATED_TAGS].ntags;
  return context;
}
