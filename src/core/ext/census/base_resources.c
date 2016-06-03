/*
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

#include <stdio.h>
#include <string.h>

#include <grpc/census.h>
#include <grpc/support/log.h>

#include "base_resources.h"
#include "gen/census.pb.h"
#include "third_party/nanopb/pb_encode.h"

// Add base RPC resource definitions for use by RPC runtime.
//
// TODO(aveitch): All of these are currently hardwired definitions encoded in
// the code in this file. These should be converted to use an external
// configuration mechanism, in which these resources are defined in a text
// file, which is compiled to .pb format and read by still-to-be-written
// configuration functions.

// Structure representing a MeasurementUnit proto.
typedef struct {
  int32_t prefix;
  int n_numerators;
  const google_census_Resource_BasicUnit *numerators;
  int n_denominators;
  const google_census_Resource_BasicUnit *denominators;
} measurement_unit;

// Encode a nanopb string. Expects the string argument to be passed in as `arg`.
static bool encode_string(pb_ostream_t *stream, const pb_field_t *field,
                          void *const *arg) {
  if (!pb_encode_tag_for_field(stream, field)) {
    return false;
  }
  return pb_encode_string(stream, (uint8_t *)*arg, strlen((const char *)*arg));
}

// Encode the numerators part of a measurement_unit (passed in as `arg`).
static bool encode_numerators(pb_ostream_t *stream, const pb_field_t *field,
                              void *const *arg) {
  const measurement_unit *mu = (const measurement_unit *)*arg;
  for (int i = 0; i < mu->n_numerators; i++) {
    if (!pb_encode_tag_for_field(stream, field)) {
      return false;
    }
    if (!pb_encode_varint(stream, mu->numerators[i])) {
      return false;
    }
  }
  return true;
}

// Encode the denominators part of a measurement_unit (passed in as `arg`).
static bool encode_denominators(pb_ostream_t *stream, const pb_field_t *field,
                                void *const *arg) {
  const measurement_unit *mu = (const measurement_unit *)*arg;
  for (int i = 0; i < mu->n_denominators; i++) {
    if (!pb_encode_tag_for_field(stream, field)) {
      return false;
    }
    if (!pb_encode_varint(stream, mu->numerators[i])) {
      return false;
    }
  }
  return true;
}

// Define a Resource, given the important details. Encodes a protobuf, which
// is then passed to census_define_resource.
static void define_resource(const char *name, const char *description,
                            const measurement_unit *unit) {
  // nanopb generated type for Resource. Initialize encoding functions to NULL
  // since we can't directly initialize them due to embedded union in struct.
  google_census_Resource resource = {
      {{NULL}, (void *)name},
      {{NULL}, (void *)description},
      true,  // has_unit
      {true, unit->prefix, {{NULL}, (void *)unit}, {{NULL}, (void *)unit}}};
  resource.name.funcs.encode = &encode_string;
  resource.description.funcs.encode = &encode_string;
  resource.unit.numerator.funcs.encode = &encode_numerators;
  resource.unit.denominator.funcs.encode = &encode_denominators;

  // Buffer for storing encoded proto.
  uint8_t buffer[512];
  pb_ostream_t stream = pb_ostream_from_buffer(buffer, 512);
  if (!pb_encode(&stream, google_census_Resource_fields, &resource)) {
    gpr_log(GPR_ERROR, "Error encoding resource %s.", name);
    return;
  }
  int32_t mid = census_define_resource(buffer, stream.bytes_written);
  if (mid < 0) {
    gpr_log(GPR_ERROR, "Error defining resource %s.", name);
  }
}

// Define a resource for client RPC latency.
static void define_client_rpc_latency_resource() {
  google_census_Resource_BasicUnit numerator =
      google_census_Resource_BasicUnit_SECS;
  measurement_unit unit = {0, 1, &numerator, 0, NULL};
  define_resource("client_rpc_latency", "Client RPC latency in seconds", &unit);
}

// Define a resource for server RPC latency.
static void define_server_rpc_latency_resource() {
  google_census_Resource_BasicUnit numerator =
      google_census_Resource_BasicUnit_SECS;
  measurement_unit unit = {0, 1, &numerator, 0, NULL};
  define_resource("server_rpc_latency", "Server RPC latency in seconds", &unit);
}

// Define all base resources. This should be called by census initialization.
void define_base_resources() {
  define_client_rpc_latency_resource();
  define_server_rpc_latency_resource();
}
