/*
 *
 * Copyright 2020 gRPC authors.
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
#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVAL_ARGS_UTIL_CC
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVAL_ARGS_UTIL_CC

#include "src/core/lib/security/authorization/eval_args_util.h"

grpc::string_ref get_key(grpc_linked_mdelem* elem) {
  const grpc_slice& key = GRPC_MDKEY(elem->md);
  return grpc::string_ref(reinterpret_cast<const char*>(
      GRPC_SLICE_START_PTR(key), GRPC_SLICE_LENGTH(key)));
}

grpc::string_ref get_value(grpc_linked_mdelem* elem) {
  const grpc_slice& val = GRPC_MDVALUE(elem->md);
  return grpc::string_ref(reinterpret_cast<const char*>(
      GRPC_SLICE_START_PTR(val), GRPC_SLICE_LENGTH(val)));
}

grpc::string_ref get_path(grpc_metadata_batch* metadata) {
  grpc_linked_mdelem* path = metadata->idx.array[GRPC_BATCH_PATH];
  return get_value(path);
}

grpc::string_ref get_host(grpc_metadata_batch* metadata) {
  grpc_linked_mdelem* host = metadata->idx.array[GRPC_BATCH_PATH];
  return get_value(host);
}

grpc::string_ref get_method(grpc_metadata_batch* metadata) {
  grpc_linked_mdelem* method = metadata->idx.array[GRPC_BATCH_METHOD];
  return get_value(method);
}

std::multimap<grpc::string_ref, grpc::string_ref> get_headers(
    grpc_metadata_batch* metadata) {
  std::multimap<grpc::string_ref, grpc::string_ref> grpc_metadata;
  for (grpc_linked_mdelem* elem = metadata->list.head; elem != nullptr;
       elem = elem->next) {
    grpc_metadata.emplace(get_key(elem), get_value(elem));
  }
}

grpc::string_ref get_uri(grpc_auth_context* auth_context) {
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context, GRPC_PEER_SPIFFE_ID_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop == nullptr) return grpc::string_ref("", 0);
  if (strncmp(prop->value, GRPC_PEER_SPIFFE_ID_PROPERTY_NAME,
              prop->value_length) != 0) {
    return grpc::string_ref("", 0);
  }
  if (grpc_auth_property_iterator_next(&it) != nullptr)
    return grpc::string_ref("", 0);
  return grpc::string_ref(
      reinterpret_cast<const char*>(prop->value, prop->value_length));
}

grpc::string_ref get_server_name(grpc_auth_context* auth_context) {
  grpc_auth_property_iterator it = grpc_auth_context_find_properties_by_name(
      auth_context, GRPC_X509_CN_PROPERTY_NAME);
  const grpc_auth_property* prop = grpc_auth_property_iterator_next(&it);
  if (prop == nullptr) return grpc::string_ref("", 0);
  if (strncmp(prop->value, GRPC_X509_CN_PROPERTY_NAME, prop->value_length) !=
      0) {
    return grpc::string_ref("", 0);
  }
  if (grpc_auth_property_iterator_next(&it) != nullptr)
    return grpc::string_ref("", 0);
  return grpc::string_ref(
      reinterpret_cast<const char*>(prop->value, prop->value_length));
}

// grpc::string_ref getPrincipal(grpc_auth_context* auth_context) {

// }

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_EVAL_ARGS_UTIL_CC