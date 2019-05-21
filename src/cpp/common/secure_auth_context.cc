/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/cpp/common/secure_auth_context.h"

#include <grpc/grpc_security.h>

namespace grpc {

std::vector<grpc::string_ref> SecureAuthContext::GetPeerIdentity() const {
  if (ctx_ == nullptr) {
    return std::vector<grpc::string_ref>();
  }
  grpc_auth_property_iterator iter =
      grpc_auth_context_peer_identity(ctx_.get());
  std::vector<grpc::string_ref> identity;
  const grpc_auth_property* property = nullptr;
  while ((property = grpc_auth_property_iterator_next(&iter))) {
    identity.push_back(
        grpc::string_ref(property->value, property->value_length));
  }
  return identity;
}

grpc::string SecureAuthContext::GetPeerIdentityPropertyName() const {
  if (ctx_ == nullptr) {
    return "";
  }
  const char* name = grpc_auth_context_peer_identity_property_name(ctx_.get());
  return name == nullptr ? "" : name;
}

std::vector<grpc::string_ref> SecureAuthContext::FindPropertyValues(
    const grpc::string& name) const {
  if (ctx_ == nullptr) {
    return std::vector<grpc::string_ref>();
  }
  grpc_auth_property_iterator iter =
      grpc_auth_context_find_properties_by_name(ctx_.get(), name.c_str());
  const grpc_auth_property* property = nullptr;
  std::vector<grpc::string_ref> values;
  while ((property = grpc_auth_property_iterator_next(&iter))) {
    values.push_back(grpc::string_ref(property->value, property->value_length));
  }
  return values;
}

AuthPropertyIterator SecureAuthContext::begin() const {
  if (ctx_ != nullptr) {
    grpc_auth_property_iterator iter =
        grpc_auth_context_property_iterator(ctx_.get());
    const grpc_auth_property* property =
        grpc_auth_property_iterator_next(&iter);
    return AuthPropertyIterator(property, &iter);
  } else {
    return end();
  }
}

AuthPropertyIterator SecureAuthContext::end() const {
  return AuthPropertyIterator();
}

void SecureAuthContext::AddProperty(const grpc::string& key,
                                    const grpc::string_ref& value) {
  if (ctx_ == nullptr) return;
  grpc_auth_context_add_property(ctx_.get(), key.c_str(), value.data(),
                                 value.size());
}

bool SecureAuthContext::SetPeerIdentityPropertyName(const grpc::string& name) {
  if (ctx_ == nullptr) return false;
  return grpc_auth_context_set_peer_identity_property_name(ctx_.get(),
                                                           name.c_str()) != 0;
}

bool SecureAuthContext::IsPeerAuthenticated() const {
  if (ctx_ == nullptr) return false;
  return grpc_auth_context_peer_is_authenticated(ctx_.get()) != 0;
}

}  // namespace grpc
