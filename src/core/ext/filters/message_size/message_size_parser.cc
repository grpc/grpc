//
// Copyright 2019 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/message_size/message_size_parser.h"

#include "src/core/lib/gpr/string.h"

namespace {
size_t g_message_size_parser_index;

// Consumes all the errors in the vector and forms a referencing error from
// them. If the vector is empty, return GRPC_ERROR_NONE.
template <size_t N>
grpc_error* CreateErrorFromVector(
    const char* desc, grpc_core::InlinedVector<grpc_error*, N>* error_list) {
  grpc_error* error = GRPC_ERROR_NONE;
  if (error_list->size() != 0) {
    error = GRPC_ERROR_CREATE_REFERENCING_FROM_STATIC_STRING(
        desc, error_list->data(), error_list->size());
    // Remove refs to all errors in error_list.
    for (size_t i = 0; i < error_list->size(); i++) {
      GRPC_ERROR_UNREF((*error_list)[i]);
    }
    error_list->clear();
  }
  return error;
}
}  // namespace

namespace grpc_core {

UniquePtr<ServiceConfigParsedObject> MessageSizeParser::ParsePerMethodParams(
    const grpc_json* json, grpc_error** error) {
  GPR_DEBUG_ASSERT(error != nullptr && *error == GRPC_ERROR_NONE);
  int max_request_message_bytes = -1;
  int max_response_message_bytes = -1;
  InlinedVector<grpc_error*, 4> error_list;
  for (grpc_json* field = json->child; field != nullptr; field = field->next) {
    if (field->key == nullptr) continue;
    if (strcmp(field->key, "maxRequestMessageBytes") == 0) {
      if (max_request_message_bytes >= 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxRequestMessageBytes error:Duplicate entry"));
      } else if (field->type != GRPC_JSON_STRING &&
                 field->type != GRPC_JSON_NUMBER) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxRequestMessageBytes error:should be of type number"));
      } else {
        max_request_message_bytes = gpr_parse_nonnegative_int(field->value);
        if (max_request_message_bytes == -1) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:maxRequestMessageBytes error:should be non-negative"));
        }
      }
    } else if (strcmp(field->key, "maxResponseMessageBytes") == 0) {
      if (max_response_message_bytes >= 0) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxResponseMessageBytes error:Duplicate entry"));
      } else if (field->type != GRPC_JSON_STRING &&
                 field->type != GRPC_JSON_NUMBER) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "field:maxResponseMessageBytes error:should be of type number"));
      } else {
        max_response_message_bytes = gpr_parse_nonnegative_int(field->value);
        if (max_response_message_bytes == -1) {
          error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
              "field:maxResponseMessageBytes error:should be non-negative"));
        }
      }
    }
  }
  if (!error_list.empty()) {
    *error = CreateErrorFromVector("Message size parser", &error_list);
    return nullptr;
  }
  return UniquePtr<ServiceConfigParsedObject>(New<MessageSizeParsedObject>(
      max_request_message_bytes, max_response_message_bytes));
}

void MessageSizeParser::Register() {
  g_message_size_parser_index = ServiceConfig::RegisterParser(
      UniquePtr<ServiceConfigParser>(New<MessageSizeParser>()));
}

size_t MessageSizeParser::ParserIndex() { return g_message_size_parser_index; }
}  // namespace grpc_core
