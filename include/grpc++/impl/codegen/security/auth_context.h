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

#ifndef GRPCXX_IMPL_CODEGEN_SECURITY_AUTH_CONTEXT_H
#define GRPCXX_IMPL_CODEGEN_SECURITY_AUTH_CONTEXT_H

#include <iterator>
#include <vector>

#include <grpc++/impl/codegen/config.h>
#include <grpc++/impl/codegen/string_ref.h>

struct grpc_auth_context;
struct grpc_auth_property;
struct grpc_auth_property_iterator;

namespace grpc {
class SecureAuthContext;

typedef std::pair<grpc::string_ref, grpc::string_ref> AuthProperty;

class AuthPropertyIterator
    : public std::iterator<std::input_iterator_tag, const AuthProperty> {
 public:
  ~AuthPropertyIterator();
  AuthPropertyIterator& operator++();
  AuthPropertyIterator operator++(int);
  bool operator==(const AuthPropertyIterator& rhs) const;
  bool operator!=(const AuthPropertyIterator& rhs) const;
  const AuthProperty operator*();

 protected:
  AuthPropertyIterator();
  AuthPropertyIterator(const grpc_auth_property* property,
                       const grpc_auth_property_iterator* iter);

 private:
  friend class SecureAuthContext;
  const grpc_auth_property* property_;
  // The following items form a grpc_auth_property_iterator.
  const grpc_auth_context* ctx_;
  size_t index_;
  const char* name_;
};

/// Class encapsulating the Authentication Information.
///
/// It includes the secure identity of the peer, the type of secure transport
/// used as well as any other properties required by the authorization layer.
class AuthContext {
 public:
  virtual ~AuthContext() {}

  /// Returns true if the peer is authenticated.
  virtual bool IsPeerAuthenticated() const = 0;

  /// A peer identity.
  ///
  /// It is, in general, comprised of one or more properties (in which case they
  /// have the same name).
  virtual std::vector<grpc::string_ref> GetPeerIdentity() const = 0;
  virtual grpc::string GetPeerIdentityPropertyName() const = 0;

  /// Returns all the property values with the given name.
  virtual std::vector<grpc::string_ref> FindPropertyValues(
      const grpc::string& name) const = 0;

  /// Iteration over all the properties.
  virtual AuthPropertyIterator begin() const = 0;
  virtual AuthPropertyIterator end() const = 0;

  // Mutation functions: should only be used by an AuthMetadataProcessor.
  virtual void AddProperty(const grpc::string& key,
                           const grpc::string_ref& value) = 0;
  virtual bool SetPeerIdentityPropertyName(const grpc::string& name) = 0;
};

}  // namespace grpc

#endif  // GRPCXX_IMPL_CODEGEN_SECURITY_AUTH_CONTEXT_H
