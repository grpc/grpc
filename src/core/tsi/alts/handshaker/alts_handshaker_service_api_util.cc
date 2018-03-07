/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/tsi/alts/handshaker/alts_handshaker_service_api_util.h"

void add_repeated_field(repeated_field** head, const void* data) {
  repeated_field* field =
      static_cast<repeated_field*>(gpr_zalloc(sizeof(*field)));
  field->data = data;
  if (*head == nullptr) {
    *head = field;
    (*head)->next = nullptr;
  } else {
    field->next = *head;
    *head = field;
  }
}

void destroy_repeated_field_list_identity(repeated_field* head) {
  repeated_field* field = head;
  while (field != nullptr) {
    repeated_field* next_field = field->next;
    const grpc_gcp_identity* identity =
        static_cast<const grpc_gcp_identity*>(field->data);
    destroy_slice(static_cast<grpc_slice*>(identity->hostname.arg));
    destroy_slice(static_cast<grpc_slice*>(identity->service_account.arg));
    gpr_free((void*)identity);
    gpr_free(field);
    field = next_field;
  }
}

void destroy_repeated_field_list_string(repeated_field* head) {
  repeated_field* field = head;
  while (field != nullptr) {
    repeated_field* next_field = field->next;
    destroy_slice((grpc_slice*)field->data);
    gpr_free(field);
    field = next_field;
  }
}

grpc_slice* create_slice(const char* data, size_t size) {
  grpc_slice slice = grpc_slice_from_copied_buffer(data, size);
  grpc_slice* cb_slice =
      static_cast<grpc_slice*>(gpr_zalloc(sizeof(*cb_slice)));
  memcpy(cb_slice, &slice, sizeof(*cb_slice));
  return cb_slice;
}

void destroy_slice(grpc_slice* slice) {
  if (slice != nullptr) {
    grpc_slice_unref(*slice);
    gpr_free(slice);
  }
}

bool encode_string_or_bytes_cb(pb_ostream_t* stream, const pb_field_t* field,
                               void* const* arg) {
  grpc_slice* slice = static_cast<grpc_slice*>(*arg);
  if (!pb_encode_tag_for_field(stream, field)) return false;
  return pb_encode_string(stream, GRPC_SLICE_START_PTR(*slice),
                          GRPC_SLICE_LENGTH(*slice));
}

bool encode_repeated_identity_cb(pb_ostream_t* stream, const pb_field_t* field,
                                 void* const* arg) {
  repeated_field* var = static_cast<repeated_field*>(*arg);
  while (var != nullptr) {
    if (!pb_encode_tag_for_field(stream, field)) return false;
    if (!pb_encode_submessage(stream, grpc_gcp_Identity_fields,
                              (grpc_gcp_identity*)var->data))
      return false;
    var = var->next;
  }
  return true;
}

bool encode_repeated_string_cb(pb_ostream_t* stream, const pb_field_t* field,
                               void* const* arg) {
  repeated_field* var = static_cast<repeated_field*>(*arg);
  while (var != nullptr) {
    if (!pb_encode_tag_for_field(stream, field)) return false;
    const grpc_slice* slice = static_cast<const grpc_slice*>(var->data);
    if (!pb_encode_string(stream, GRPC_SLICE_START_PTR(*slice),
                          GRPC_SLICE_LENGTH(*slice)))
      return false;
    var = var->next;
  }
  return true;
}

bool decode_string_or_bytes_cb(pb_istream_t* stream, const pb_field_t* field,
                               void** arg) {
  grpc_slice slice = grpc_slice_malloc(stream->bytes_left);
  grpc_slice* cb_slice =
      static_cast<grpc_slice*>(gpr_zalloc(sizeof(*cb_slice)));
  memcpy(cb_slice, &slice, sizeof(*cb_slice));
  if (!pb_read(stream, GRPC_SLICE_START_PTR(*cb_slice), stream->bytes_left))
    return false;
  *arg = cb_slice;
  return true;
}

bool decode_repeated_identity_cb(pb_istream_t* stream, const pb_field_t* field,
                                 void** arg) {
  grpc_gcp_identity* identity =
      static_cast<grpc_gcp_identity*>(gpr_zalloc(sizeof(*identity)));
  identity->hostname.funcs.decode = decode_string_or_bytes_cb;
  identity->service_account.funcs.decode = decode_string_or_bytes_cb;
  add_repeated_field(reinterpret_cast<repeated_field**>(arg), identity);
  if (!pb_decode(stream, grpc_gcp_Identity_fields, identity)) return false;
  return true;
}

bool decode_repeated_string_cb(pb_istream_t* stream, const pb_field_t* field,
                               void** arg) {
  grpc_slice slice = grpc_slice_malloc(stream->bytes_left);
  grpc_slice* cb_slice =
      static_cast<grpc_slice*>(gpr_zalloc(sizeof(*cb_slice)));
  memcpy(cb_slice, &slice, sizeof(grpc_slice));
  if (!pb_read(stream, GRPC_SLICE_START_PTR(*cb_slice), stream->bytes_left))
    return false;
  add_repeated_field(reinterpret_cast<repeated_field**>(arg), cb_slice);
  return true;
}
