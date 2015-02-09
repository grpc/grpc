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
#include <string.h>

#include <grpc/census.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include "src/core/support/string.h"

struct census_tag_set {
  int ntags;     /* Number of tags in this tag set. */
  int size;      /* Max capacity of this tag set. */
  char **keys;   /* Tag keys */
  char **values; /* Tag values */
};

static void cts_validate(census_tag_set *tags) {
  GPR_ASSERT((tags->size == 0 && tags->ntags == 0 && tags->keys == NULL &&
              tags->values == NULL) ||
             (tags->size != 0 && tags->ntags != 0 && tags->keys != NULL &&
              tags->values != NULL));
}

/* Expand the number of tags stored */
static void cts_extend(census_tag_set *tags) {
  char **new_keys;
  char **new_values;
  tags->size = tags->size == 0 ? 4 : 2 * tags->size;
  new_keys = gpr_malloc(tags->size * sizeof(char *));
  new_values = gpr_malloc(tags->size * sizeof(char *));
  memcpy(new_keys, tags->keys, tags->ntags * sizeof(char *));
  memcpy(new_values, tags->values, tags->ntags * sizeof(char *));
  gpr_free(tags->keys);
  gpr_free(tags->values);
  tags->keys = new_keys;
  tags->values = new_values;
}

void census_tag_set_add(census_tag_set *tags, const char *key,
                        const char *value) {
  int i;

  cts_validate(tags);

  /* replace existing value if key already exists */
  for (i = 0; i < tags->ntags; i++) {
    if (strcmp(key, tags->keys[i]) == 0) {
      char *replace = gpr_strdup(value);
      gpr_free(tags->values[i]);
      tags->values[i] = replace;
      return;
    }
  }

  /* insert new tag */
  if (tags->size == tags->ntags) {
    cts_extend(tags);
  }
  tags->keys[tags->ntags] = gpr_strdup(key);
  tags->values[tags->ntags] = gpr_strdup(value);
  tags->ntags++;
}

void census_tag_set_reset(census_tag_set *tags) {
  int i;
  cts_validate(tags);
  for (i = 0; i < tags->ntags; i++) {
    gpr_free(tags->keys[i]);
    gpr_free(tags->values[i]);
  }
  gpr_free(tags->keys);
  gpr_free(tags->values);
  tags->keys = NULL;
  tags->values = NULL;
  tags->ntags = 0;
  tags->size = 0;
}

static void cts_combine(census_tag_set *tags, const census_tag_set *add) {
  int i;
  for (i = 0; i < add->ntags; i++) {
    census_tag_set_add(tags, add->keys[i], add->values[i]);
  }
}

struct census_context {
  gpr_uint64 op_id;    /* Operation identifier - unique per-context */
  gpr_uint64 trace_id; /* Globally unique trace identifier */
  /* TODO(aveitch): This version of context explicitly stores all tags. We
     need to change this to using a hash of the tag set, and storing
     the tags themselves seperately. This will enable significantly more
     memory and CPU efficiency. */
  census_tag_set *tags; /* set of tags associated with this context */
};

census_context *census_op_start(const census_context *parent,
                                const census_tag_set *tags) {
  census_context *child = gpr_malloc(sizeof(census_context));
  child->tags = (census_tag_set *)gpr_malloc(sizeof(census_tag_set));
  /* TODO(aveitch): All ID's should be properly random */
  if (parent == NULL) {
    child->op_id = 0;
    child->trace_id = 0;
  } else {
    child->op_id = parent->op_id + (gpr_intptr)child;
    child->trace_id = parent->trace_id;
    cts_combine(child->tags, parent->tags);
  }
  if (tags != NULL) {
    cts_combine(child->tags, tags);
  }
  /* TODO(aveitch): write log entry */
  return child;
}

void census_end_op(census_context *context) {
  census_tag_set_reset(context->tags);
  gpr_free(context->tags);
  context->tags = NULL;
  context->op_id = context->trace_id = 0xbad;
  /* TODO(aveitch): write log entry */
}

void census_trace(const census_context *context, const char *s) {
  /* TODO(aveitch): write log entry */
}

void census_record_metric(census_context *context, const char *name,
                          double value) {
  /* TODO(aveitch): write log entry */
}

size_t census_context_serialize(const census_context *context, char *buffer,
                                size_t n) {
  return 0;
}

void census_context_deserialize(census_context *context, const char *buffer) {}
