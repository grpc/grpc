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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/json/json.h"

grpc_json *grpc_json_create(grpc_json_type type) {
  grpc_json *json = gpr_malloc(sizeof(*json));
  memset(json, 0, sizeof(*json));
  json->type = type;

  return json;
}

void grpc_json_destroy(grpc_json *json) {
  while (json->child) {
    grpc_json_destroy(json->child);
  }

  if (json->next) {
    json->next->prev = json->prev;
  }

  if (json->prev) {
    json->prev->next = json->next;
  } else if (json->parent) {
    json->parent->child = json->next;
  }

  gpr_free(json);
}

int grpc_json_cmp(const grpc_json* json1, const grpc_json* json2) {
  if (json1 == NULL) {
    if (json2 != NULL) return 1;
    return 0;  // Both NULL.
  } else {
    if (json2 == NULL) return -1;
  }
  // Compare type.
  if (json1->type > json2->type) return 1;
  if (json1->type < json2->type) return -1;
  // Compare key.
  if (json1->key == NULL) {
    if (json2->key != NULL) return -1;
  } else {
    if (json2->key == NULL) return 1;
    int retval = strcmp(json1->key, json2->key);
    if (retval != 0) return retval;
  }
  // Compare value.
  if (json1->value == NULL) {
    if (json2->value != NULL) return -1;
  } else {
    if (json2->value == NULL) return 1;
    int retval = strcmp(json1->value, json2->value);
    if (retval != 0) return retval;
  }
  // Recursively compare the next pointer.
  int retval = grpc_json_cmp(json1->next, json2->next);
  if (retval != 0) return retval;
  // Recursively compare the child pointer.
  retval = grpc_json_cmp(json1->child, json2->child);
  if (retval != 0) return retval;
  // Both are the same.
  return 0;
}

grpc_json_tree* grpc_json_tree_create(const char* json_string) {
  grpc_json_tree* tree = gpr_malloc(sizeof(*tree));
  tree->string = gpr_strdup(json_string);
  tree->root = grpc_json_parse_string(tree->string);
  gpr_ref_init(&tree->refs, 1);
  return tree;
}

grpc_json_tree* grpc_json_tree_ref(grpc_json_tree* tree) {
  gpr_ref(&tree->refs);
  return tree;
}

void grpc_json_tree_unref(grpc_json_tree* tree) {
  if (gpr_unref(&tree->refs)) {
    grpc_json_destroy(tree->root);
    gpr_free(tree->string);
    gpr_free(tree);
  }
}
