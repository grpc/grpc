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

#include "src/core/ext/filters/client_channel/xds/xds_bootstrap.h"

#include <errno.h>
#include <stdlib.h>

#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

UniquePtr<XdsBootstrap> XdsBootstrap::ReadFromFile(grpc_error** error) {
  UniquePtr<char> path(gpr_getenv("GRPC_XDS_BOOTSTRAP"));
  if (path == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "GRPC_XDS_BOOTSTRAP env var not set");
    return nullptr;
  }
  grpc_slice contents;
  *error = grpc_load_file(path.get(), /*add_null_terminator=*/true, &contents);
  if (*error != GRPC_ERROR_NONE) return nullptr;
  return MakeUnique<XdsBootstrap>(contents, error);
}

XdsBootstrap::XdsBootstrap(grpc_slice contents, grpc_error** error)
    : contents_(contents) {
  tree_ = grpc_json_parse_string_with_len(
      reinterpret_cast<char*>(GPR_SLICE_START_PTR(contents_)),
      GPR_SLICE_LENGTH(contents_));
  if (tree_ == nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "failed to parse bootstrap file JSON");
    return;
  }
  if (tree_->type != GRPC_JSON_OBJECT || tree_->key != nullptr) {
    *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "malformed JSON in bootstrap file");
    return;
  }
  InlinedVector<grpc_error*, 1> error_list;
  bool seen_xds_server = false;
  bool seen_node = false;
  for (grpc_json* child = tree_->child; child != nullptr; child = child->next) {
    if (child->key == nullptr) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("JSON key is null"));
    } else if (strcmp(child->key, "xds_server") == 0) {
      if (child->type != GRPC_JSON_OBJECT) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"xds_server\" field is not an object"));
      }
      if (seen_xds_server) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "duplicate \"xds_server\" field"));
      }
      seen_xds_server = true;
      grpc_error* parse_error = ParseXdsServer(child);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    } else if (strcmp(child->key, "node") == 0) {
      if (child->type != GRPC_JSON_OBJECT) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"node\" field is not an object"));
      }
      if (seen_node) {
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("duplicate \"node\" field"));
      }
      seen_node = true;
      grpc_error* parse_error = ParseNode(child);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  if (!seen_xds_server) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"xds_server\" field not present"));
  }
  *error = GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing xds bootstrap file",
                                         &error_list);
}

XdsBootstrap::~XdsBootstrap() {
  grpc_json_destroy(tree_);
  grpc_slice_unref_internal(contents_);
}

grpc_error* XdsBootstrap::ParseXdsServer(grpc_json* json) {
  InlinedVector<grpc_error*, 1> error_list;
  server_uri_ = nullptr;
  bool seen_channel_creds = false;
  for (grpc_json* child = json->child; child != nullptr; child = child->next) {
    if (child->key == nullptr) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("JSON key is null"));
    } else if (strcmp(child->key, "server_uri") == 0) {
      if (child->type != GRPC_JSON_STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"server_uri\" field is not a string"));
      }
      if (server_uri_ != nullptr) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "duplicate \"server_uri\" field"));
      }
      server_uri_ = child->value;
    } else if (strcmp(child->key, "channel_creds") == 0) {
      if (child->type != GRPC_JSON_ARRAY) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"channel_creds\" field is not an array"));
      }
      if (seen_channel_creds) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "duplicate \"channel_creds\" field"));
      }
      seen_channel_creds = true;
      grpc_error* parse_error = ParseChannelCredsArray(child);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  if (server_uri_ == nullptr) {
    error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "\"server_uri\" field not present"));
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing \"xds_server\" object",
                                       &error_list);
}

grpc_error* XdsBootstrap::ParseChannelCredsArray(grpc_json* json) {
  InlinedVector<grpc_error*, 1> error_list;
  size_t idx = 0;
  for (grpc_json *child = json->child; child != nullptr;
       child = child->next, ++idx) {
    if (child->key != nullptr) {
      char* msg;
      gpr_asprintf(&msg, "array element %" PRIuPTR " key is not null", idx);
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
    }
    if (child->type != GRPC_JSON_OBJECT) {
      char* msg;
      gpr_asprintf(&msg, "array element %" PRIuPTR " is not an object", idx);
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
    } else {
      grpc_error* parse_error = ParseChannelCreds(child, idx);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing \"channel_creds\" array",
                                       &error_list);
}

grpc_error* XdsBootstrap::ParseChannelCreds(grpc_json* json, size_t idx) {
  InlinedVector<grpc_error*, 1> error_list;
  ChannelCreds channel_creds;
  for (grpc_json* child = json->child; child != nullptr; child = child->next) {
    if (child->key == nullptr) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("JSON key is null"));
    } else if (strcmp(child->key, "type") == 0) {
      if (child->type != GRPC_JSON_STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"type\" field is not a string"));
      }
      if (channel_creds.type != nullptr) {
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("duplicate \"type\" field"));
      }
      channel_creds.type = child->value;
    } else if (strcmp(child->key, "config") == 0) {
      if (child->type != GRPC_JSON_OBJECT) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"config\" field is not an object"));
      }
      if (channel_creds.config != nullptr) {
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("duplicate \"config\" field"));
      }
      channel_creds.config = child;
    }
  }
  if (channel_creds.type != nullptr) channel_creds_.push_back(channel_creds);
  // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
  // string is not static in this case.
  if (error_list.empty()) return GRPC_ERROR_NONE;
  char* msg;
  gpr_asprintf(&msg, "errors parsing index %" PRIuPTR, idx);
  grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
  gpr_free(msg);
  for (size_t i = 0; i < error_list.size(); ++i) {
    error = grpc_error_add_child(error, error_list[i]);
  }
  return error;
}

grpc_error* XdsBootstrap::ParseNode(grpc_json* json) {
  InlinedVector<grpc_error*, 1> error_list;
  node_ = MakeUnique<Node>();
  bool seen_metadata = false;
  bool seen_locality = false;
  for (grpc_json* child = json->child; child != nullptr; child = child->next) {
    if (child->key == nullptr) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("JSON key is null"));
    } else if (strcmp(child->key, "id") == 0) {
      if (child->type != GRPC_JSON_STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"id\" field is not a string"));
      }
      if (node_->id != nullptr) {
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("duplicate \"id\" field"));
      }
      node_->id = child->value;
    } else if (strcmp(child->key, "cluster") == 0) {
      if (child->type != GRPC_JSON_STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"cluster\" field is not a string"));
      }
      if (node_->cluster != nullptr) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "duplicate \"cluster\" field"));
      }
      node_->cluster = child->value;
    } else if (strcmp(child->key, "locality") == 0) {
      if (child->type != GRPC_JSON_OBJECT) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"locality\" field is not an object"));
      }
      if (seen_locality) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "duplicate \"locality\" field"));
      }
      seen_locality = true;
      grpc_error* parse_error = ParseLocality(child);
      if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
    } else if (strcmp(child->key, "metadata") == 0) {
      if (child->type != GRPC_JSON_OBJECT) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"metadata\" field is not an object"));
      }
      if (seen_metadata) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "duplicate \"metadata\" field"));
      }
      seen_metadata = true;
      InlinedVector<grpc_error*, 1> parse_errors =
          ParseMetadataStruct(child, &node_->metadata);
      if (!parse_errors.empty()) {
        grpc_error* parse_error = GRPC_ERROR_CREATE_FROM_VECTOR(
            "errors parsing \"metadata\" object", &parse_errors);
        error_list.push_back(parse_error);
      }
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing \"node\" object",
                                       &error_list);
}

grpc_error* XdsBootstrap::ParseLocality(grpc_json* json) {
  InlinedVector<grpc_error*, 1> error_list;
  node_->locality_region = nullptr;
  node_->locality_zone = nullptr;
  node_->locality_subzone = nullptr;
  for (grpc_json* child = json->child; child != nullptr; child = child->next) {
    if (child->key == nullptr) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("JSON key is null"));
    } else if (strcmp(child->key, "region") == 0) {
      if (child->type != GRPC_JSON_STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"region\" field is not a string"));
      }
      if (node_->locality_region != nullptr) {
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("duplicate \"region\" field"));
      }
      node_->locality_region = child->value;
    } else if (strcmp(child->key, "zone") == 0) {
      if (child->type != GRPC_JSON_STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"zone\" field is not a string"));
      }
      if (node_->locality_zone != nullptr) {
        error_list.push_back(
            GRPC_ERROR_CREATE_FROM_STATIC_STRING("duplicate \"zone\" field"));
      }
      node_->locality_zone = child->value;
    } else if (strcmp(child->key, "subzone") == 0) {
      if (child->type != GRPC_JSON_STRING) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "\"subzone\" field is not a string"));
      }
      if (node_->locality_subzone != nullptr) {
        error_list.push_back(GRPC_ERROR_CREATE_FROM_STATIC_STRING(
            "duplicate \"subzone\" field"));
      }
      node_->locality_subzone = child->value;
    }
  }
  return GRPC_ERROR_CREATE_FROM_VECTOR("errors parsing \"locality\" object",
                                       &error_list);
}

InlinedVector<grpc_error*, 1> XdsBootstrap::ParseMetadataStruct(
    grpc_json* json,
    Map<const char*, XdsBootstrap::MetadataValue, StringLess>* result) {
  InlinedVector<grpc_error*, 1> error_list;
  for (grpc_json* child = json->child; child != nullptr; child = child->next) {
    if (child->key == nullptr) {
      error_list.push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("JSON key is null"));
      continue;
    }
    if (result->find(child->key) != result->end()) {
      char* msg;
      gpr_asprintf(&msg, "duplicate metadata key \"%s\"", child->key);
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
      gpr_free(msg);
    }
    MetadataValue& value = (*result)[child->key];
    grpc_error* parse_error = ParseMetadataValue(child, 0, &value);
    if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
  }
  return error_list;
}

InlinedVector<grpc_error*, 1> XdsBootstrap::ParseMetadataList(
    grpc_json* json, std::vector<MetadataValue>* result) {
  InlinedVector<grpc_error*, 1> error_list;
  size_t idx = 0;
  for (grpc_json *child = json->child; child != nullptr;
       child = child->next, ++idx) {
    if (child->key != nullptr) {
      char* msg;
      gpr_asprintf(&msg, "JSON key is non-null for index %" PRIuPTR, idx);
      error_list.push_back(GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg));
      gpr_free(msg);
    }
    result->emplace_back();
    grpc_error* parse_error = ParseMetadataValue(child, idx, &result->back());
    if (parse_error != GRPC_ERROR_NONE) error_list.push_back(parse_error);
  }
  return error_list;
}

grpc_error* XdsBootstrap::ParseMetadataValue(grpc_json* json, size_t idx,
                                             MetadataValue* result) {
  grpc_error* error = GRPC_ERROR_NONE;
  auto context_func = [json, idx]() {
    char* context;
    if (json->key != nullptr) {
      gpr_asprintf(&context, "key \"%s\"", json->key);
    } else {
      gpr_asprintf(&context, "index %" PRIuPTR, idx);
    }
    return context;
  };
  switch (json->type) {
    case GRPC_JSON_STRING:
      result->type = MetadataValue::Type::STRING;
      result->string_value = json->value;
      break;
    case GRPC_JSON_NUMBER:
      result->type = MetadataValue::Type::DOUBLE;
      errno = 0;  // To distinguish error.
      result->double_value = strtod(json->value, nullptr);
      if (errno != 0) {
        char* context = context_func();
        char* msg;
        gpr_asprintf(&msg, "error parsing numeric value for %s: \"%s\"",
                     context, json->value);
        error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(context);
        gpr_free(msg);
      }
      break;
    case GRPC_JSON_TRUE:
      result->type = MetadataValue::Type::BOOL;
      result->bool_value = true;
      break;
    case GRPC_JSON_FALSE:
      result->type = MetadataValue::Type::BOOL;
      result->bool_value = false;
      break;
    case GRPC_JSON_NULL:
      result->type = MetadataValue::Type::MD_NULL;
      break;
    case GRPC_JSON_ARRAY: {
      result->type = MetadataValue::Type::LIST;
      InlinedVector<grpc_error*, 1> error_list =
          ParseMetadataList(json, &result->list_value);
      if (!error_list.empty()) {
        // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
        // string is not static in this case.
        char* context = context_func();
        char* msg;
        gpr_asprintf(&msg, "errors parsing struct for %s", context);
        error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(context);
        gpr_free(msg);
        for (size_t i = 0; i < error_list.size(); ++i) {
          error = grpc_error_add_child(error, error_list[i]);
        }
      }
      break;
    }
    case GRPC_JSON_OBJECT: {
      result->type = MetadataValue::Type::STRUCT;
      InlinedVector<grpc_error*, 1> error_list =
          ParseMetadataStruct(json, &result->struct_value);
      if (!error_list.empty()) {
        // Can't use GRPC_ERROR_CREATE_FROM_VECTOR() here, because the error
        // string is not static in this case.
        char* context = context_func();
        char* msg;
        gpr_asprintf(&msg, "errors parsing struct for %s", context);
        error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
        gpr_free(context);
        gpr_free(msg);
        for (size_t i = 0; i < error_list.size(); ++i) {
          error = grpc_error_add_child(error, error_list[i]);
          GRPC_ERROR_UNREF(error_list[i]);
        }
      }
      break;
    }
    default:
      break;
  }
  return error;
}

}  // namespace grpc_core
