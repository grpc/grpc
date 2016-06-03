/*
 *
 * Copyright 2016, Google Inc.
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

#include "resource.h"
#include "gen/census.pb.h"
#include "third_party/nanopb/pb_decode.h"

#include <grpc/census.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <stdbool.h>
#include <string.h>

// Internal representation of a resource.
typedef struct {
  char *name;
  // Pointer to raw protobuf used in resource definition.
  uint8_t *raw_pb;
} resource;

// Protect local resource data structures.
static gpr_mu resource_lock;

// Deleteing and creating resources are relatively rare events, and should not
// be done in the critical path of performance sensitive code. We record
// current resource id's used in a simple array, and just search it each time
// we need to assign a new id, or look up a resource.
static resource **resources = NULL;

// Number of entries in *resources
static size_t n_resources = 0;

// Number of defined resources
static size_t n_defined_resources = 0;

void initialize_resources() {
  gpr_mu_init(&resource_lock);
  gpr_mu_lock(&resource_lock);
  GPR_ASSERT(resources == NULL && n_resources == 0 && n_defined_resources == 0);
  // 8 seems like a reasonable size for initial number of resources.
  n_resources = 8;
  resources = gpr_malloc(n_resources * sizeof(resource *));
  memset(resources, 0, n_resources * sizeof(resource *));
  gpr_mu_unlock(&resource_lock);
}

// Delete a resource given it's ID. Must be called with resource_lock held.
static void delete_resource_locked(size_t rid) {
  GPR_ASSERT(resources[rid] != NULL && resources[rid]->raw_pb != NULL &&
             resources[rid]->name != NULL && n_defined_resources > 0);
  gpr_free(resources[rid]->name);
  gpr_free(resources[rid]->raw_pb);
  gpr_free(resources[rid]);
  resources[rid] = NULL;
  n_defined_resources--;
}

void shutdown_resources() {
  gpr_mu_lock(&resource_lock);
  for (size_t i = 0; i < n_resources; i++) {
    if (resources[i] != NULL) {
      delete_resource_locked(i);
    }
  }
  GPR_ASSERT(n_defined_resources == 0);
  gpr_free(resources);
  resources = NULL;
  n_resources = 0;
  gpr_mu_unlock(&resource_lock);
}

// Check the contents of string fields in a resource proto.
static bool validate_string(pb_istream_t *stream, const pb_field_t *field,
                            void **arg) {
  resource *vresource = (resource *)*arg;
  switch (field->tag) {
    case google_census_Resource_name_tag:
      // Name must have at least one character
      if (stream->bytes_left == 0) {
        gpr_log(GPR_INFO, "Zero-length Resource name.");
        return false;
      }
      vresource->name = gpr_malloc(stream->bytes_left + 1);
      vresource->name[stream->bytes_left] = '\0';
      if (!pb_read(stream, (uint8_t *)vresource->name, stream->bytes_left)) {
        return false;
      }
      // Can't have same name as an existing resource.
      for (size_t i = 0; i < n_resources; i++) {
        resource *compare = resources[i];
        if (compare == vresource || compare == NULL) continue;
        if (strcmp(compare->name, vresource->name) == 0) {
          gpr_log(GPR_INFO, "Duplicate Resource name %s.", vresource->name);
          return false;
        }
      }
      break;
    case google_census_Resource_description_tag:
      // Description is optional, does not need validating, just skip.
      if (!pb_read(stream, NULL, stream->bytes_left)) {
        return false;
      }
      break;
    default:
      // No other string fields in Resource. Print warning and skip.
      gpr_log(GPR_INFO, "Unknown string field type in Resource protobuf.");
      if (!pb_read(stream, NULL, stream->bytes_left)) {
        return false;
      }
      break;
  }
  return true;
}

// Validate units field of a Resource proto.
static bool validate_units(pb_istream_t *stream, const pb_field_t *field,
                           void **arg) {
  if (field->tag == google_census_Resource_MeasurementUnit_numerator_tag) {
    *(bool *)*arg = true;  // has_numerator = true.
  }
  while (stream->bytes_left) {
    uint64_t value;
    if (!pb_decode_varint(stream, &value)) {
      return false;
    }
  }
  return true;
}

// Vlaidate the contents of a Resource proto. `id` is the intended resource id.
static bool validate_resource_pb(const uint8_t *resource_pb,
                                 size_t resource_pb_size, size_t id) {
  GPR_ASSERT(id < n_resources);
  if (resource_pb == NULL) {
    return false;
  }
  google_census_Resource vresource;
  vresource.name.funcs.decode = &validate_string;
  vresource.name.arg = resources[id];
  vresource.description.funcs.decode = &validate_string;
  vresource.unit.numerator.funcs.decode = &validate_units;
  bool has_numerator = false;
  vresource.unit.numerator.arg = &has_numerator;
  vresource.unit.denominator.funcs.decode = &validate_units;

  pb_istream_t stream =
      pb_istream_from_buffer((uint8_t *)resource_pb, resource_pb_size);
  if (!pb_decode(&stream, google_census_Resource_fields, &vresource)) {
    return false;
  }
  // A Resource must have a name, a unit, with at least one numerator.
  return (resources[id]->name != NULL && vresource.has_unit && has_numerator);
}

int32_t census_define_resource(const uint8_t *resource_pb,
                               size_t resource_pb_size) {
  // use next_id to optimize expected placement of next new resource.
  static size_t next_id = 0;
  if (resource_pb == NULL) {
    return -1;
  }
  gpr_mu_lock(&resource_lock);
  size_t id = n_resources;  // resource ID - initialize to invalid value.
  // Expand resources if needed.
  if (n_resources == n_defined_resources) {
    resource **new_resources = gpr_malloc(n_resources * 2 * sizeof(resource *));
    memcpy(new_resources, resources, n_resources * sizeof(resource *));
    memset(new_resources + n_resources, 0, n_resources * sizeof(resource *));
    gpr_free(resources);
    resources = new_resources;
    n_resources *= 2;
    id = n_defined_resources;
  } else {
    GPR_ASSERT(n_defined_resources < n_resources);
    // Find a free id.
    for (size_t base = 0; base < n_resources; base++) {
      id = (next_id + base) % n_resources;
      if (resources[id] == NULL) break;
    }
  }
  GPR_ASSERT(id < n_resources && resources[id] == NULL);
  resources[id] = gpr_malloc(sizeof(resource));
  resources[id]->name = NULL;
  // Validate pb, extract name.
  if (!validate_resource_pb(resource_pb, resource_pb_size, id)) {
    if (resources[id]->name != NULL) {
      gpr_free(resources[id]->name);
    }
    gpr_free(resources[id]);
    resources[id] = NULL;
    gpr_mu_unlock(&resource_lock);
    return -1;
  }
  next_id = (id + 1) % n_resources;
  // Make copy of raw proto, and return.
  resources[id]->raw_pb = gpr_malloc(resource_pb_size);
  memcpy(resources[id]->raw_pb, resource_pb, resource_pb_size);
  n_defined_resources++;
  gpr_mu_unlock(&resource_lock);
  return (int32_t)id;
}

void census_delete_resource(int32_t rid) {
  gpr_mu_lock(&resource_lock);
  if (rid >= 0 && (size_t)rid < n_resources && resources[rid] != NULL) {
    delete_resource_locked((size_t)rid);
  }
  gpr_mu_unlock(&resource_lock);
}

int32_t census_resource_id(const char *name) {
  gpr_mu_lock(&resource_lock);
  for (int32_t id = 0; (size_t)id < n_resources; id++) {
    if (resources[id] != NULL) {
      if (strcmp(resources[id]->name, name) == 0) {
        gpr_mu_unlock(&resource_lock);
        return id;
      }
    }
  }
  gpr_mu_unlock(&resource_lock);
  return -1;
}
