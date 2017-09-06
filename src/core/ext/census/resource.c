/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/ext/census/resource.h"
#include "third_party/nanopb/pb_decode.h"

#include <grpc/census.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <stdbool.h>
#include <string.h>

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

void initialize_resources(void) {
  gpr_mu_init(&resource_lock);
  gpr_mu_lock(&resource_lock);
  GPR_ASSERT(resources == NULL && n_resources == 0 && n_defined_resources == 0);
  gpr_mu_unlock(&resource_lock);
}

// Delete a resource given it's ID. The ID must be a valid resource ID. Must be
// called with resource_lock held.
static void delete_resource_locked(size_t rid) {
  GPR_ASSERT(resources[rid] != NULL);
  gpr_free(resources[rid]->name);
  gpr_free(resources[rid]->description);
  gpr_free(resources[rid]->numerators);
  gpr_free(resources[rid]->denominators);
  gpr_free(resources[rid]);
  resources[rid] = NULL;
  n_defined_resources--;
}

void shutdown_resources(void) {
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
      if (stream->bytes_left == 0) {
        return true;
      }
      vresource->description = gpr_malloc(stream->bytes_left + 1);
      vresource->description[stream->bytes_left] = '\0';
      if (!pb_read(stream, (uint8_t *)vresource->description,
                   stream->bytes_left)) {
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

// Decode numerators/denominators in a stream. The `count` and `bup`
// (BasicUnit pointer) are pointers to the approriate fields in a resource
// struct.
static bool validate_units_helper(pb_istream_t *stream, int *count,
                                  google_census_Resource_BasicUnit **bup) {
  while (stream->bytes_left) {
    (*count)++;
    // Have to allocate a new array of values. Normal case is 0 or 1, so
    // this should normally not be an issue.
    google_census_Resource_BasicUnit *new_bup =
        gpr_malloc((size_t)*count * sizeof(google_census_Resource_BasicUnit));
    if (*count != 1) {
      memcpy(new_bup, *bup,
             (size_t)(*count - 1) * sizeof(google_census_Resource_BasicUnit));
      gpr_free(*bup);
    }
    *bup = new_bup;
    uint64_t value;
    if (!pb_decode_varint(stream, &value)) {
      return false;
    }
    *(*bup + *count - 1) = (google_census_Resource_BasicUnit)value;
  }
  return true;
}

// Validate units field of a Resource proto.
static bool validate_units(pb_istream_t *stream, const pb_field_t *field,
                           void **arg) {
  resource *vresource = (resource *)(*arg);
  switch (field->tag) {
    case google_census_Resource_MeasurementUnit_numerator_tag:
      return validate_units_helper(stream, &vresource->n_numerators,
                                   &vresource->numerators);
      break;
    case google_census_Resource_MeasurementUnit_denominator_tag:
      return validate_units_helper(stream, &vresource->n_denominators,
                                   &vresource->denominators);
      break;
    default:
      gpr_log(GPR_ERROR, "Unknown field type.");
      return false;
      break;
  }
  return true;
}

// Validate the contents of a Resource proto. `id` is the intended resource id.
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
  vresource.description.arg = resources[id];
  vresource.unit.numerator.funcs.decode = &validate_units;
  vresource.unit.numerator.arg = resources[id];
  vresource.unit.denominator.funcs.decode = &validate_units;
  vresource.unit.denominator.arg = resources[id];

  pb_istream_t stream =
      pb_istream_from_buffer((uint8_t *)resource_pb, resource_pb_size);
  if (!pb_decode(&stream, google_census_Resource_fields, &vresource)) {
    return false;
  }
  // A Resource must have a name, a unit, with at least one numerator.
  return (resources[id]->name != NULL && vresource.has_unit &&
          resources[id]->n_numerators > 0);
}

// Allocate a blank resource, and return associated ID. Must be called with
// resource_lock held.
size_t allocate_resource(void) {
  // use next_id to optimize expected placement of next new resource.
  static size_t next_id = 0;
  size_t id = n_resources;  // resource ID - initialize to invalid value.
  // Expand resources if needed.
  if (n_resources == n_defined_resources) {
    size_t new_n_resources = n_resources ? n_resources * 2 : 2;
    resource **new_resources = gpr_malloc(new_n_resources * sizeof(resource *));
    if (n_resources != 0) {
      memcpy(new_resources, resources, n_resources * sizeof(resource *));
    }
    memset(new_resources + n_resources, 0,
           (new_n_resources - n_resources) * sizeof(resource *));
    gpr_free(resources);
    resources = new_resources;
    n_resources = new_n_resources;
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
  memset(resources[id], 0, sizeof(resource));
  n_defined_resources++;
  next_id = (id + 1) % n_resources;
  return id;
}

int32_t census_define_resource(const uint8_t *resource_pb,
                               size_t resource_pb_size) {
  if (resource_pb == NULL) {
    return -1;
  }
  gpr_mu_lock(&resource_lock);
  size_t id = allocate_resource();
  // Validate pb, extract name.
  if (!validate_resource_pb(resource_pb, resource_pb_size, id)) {
    delete_resource_locked(id);
    gpr_mu_unlock(&resource_lock);
    return -1;
  }
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
    if (resources[id] != NULL && strcmp(resources[id]->name, name) == 0) {
      gpr_mu_unlock(&resource_lock);
      return id;
    }
  }
  gpr_mu_unlock(&resource_lock);
  return -1;
}

int32_t define_resource(const resource *base) {
  GPR_ASSERT(base != NULL && base->name != NULL && base->n_numerators > 0 &&
             base->numerators != NULL);
  gpr_mu_lock(&resource_lock);
  size_t id = allocate_resource();
  size_t len = strlen(base->name) + 1;
  resources[id]->name = gpr_malloc(len);
  memcpy(resources[id]->name, base->name, len);
  if (base->description) {
    len = strlen(base->description) + 1;
    resources[id]->description = gpr_malloc(len);
    memcpy(resources[id]->description, base->description, len);
  }
  resources[id]->prefix = base->prefix;
  resources[id]->n_numerators = base->n_numerators;
  len = (size_t)base->n_numerators * sizeof(*base->numerators);
  resources[id]->numerators = gpr_malloc(len);
  memcpy(resources[id]->numerators, base->numerators, len);
  resources[id]->n_denominators = base->n_denominators;
  if (base->n_denominators != 0) {
    len = (size_t)base->n_denominators * sizeof(*base->denominators);
    resources[id]->denominators = gpr_malloc(len);
    memcpy(resources[id]->denominators, base->denominators, len);
  }
  gpr_mu_unlock(&resource_lock);
  return (int32_t)id;
}
