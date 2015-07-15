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

#ifndef GRPCXX_AUTH_CONTEXT_H
#define GRPCXX_AUTH_CONTEXT_H

#include <iterator>
#include <vector>

#include <grpc++/config.h>

struct grpc_auth_context;
struct grpc_auth_property;
struct grpc_auth_property_iterator;

namespace grpc {
class SecureAuthContext;

class AuthContext {
 public:
  typedef std::pair<grpc::string, grpc::string> Property;
  class PropertyIterator
      : public std::iterator<std::input_iterator_tag, const Property> {
   public:
    ~PropertyIterator();
    PropertyIterator& operator++();
    PropertyIterator operator++(int);
    bool operator==(const PropertyIterator& rhs) const;
    bool operator!=(const PropertyIterator& rhs) const;
    const Property operator*();

   private:
    friend SecureAuthContext;
    PropertyIterator();
    PropertyIterator(const grpc_auth_property* property,
                     const grpc_auth_property_iterator* iter);
    const grpc_auth_property* property_;
    // The following items form a grpc_auth_property_iterator.
    const grpc_auth_context* ctx_;
    size_t index_;
    const char* name_;
  };

  virtual ~AuthContext() {}

  // A peer identity, in general is one or more properties (in which case they
  // have the same name).
  virtual std::vector<grpc::string> GetPeerIdentity() const = 0;
  virtual grpc::string GetPeerIdentityPropertyName() const = 0;

  // Returns all the property values with the given name.
  virtual std::vector<grpc::string> FindPropertyValues(
      const grpc::string& name) const = 0;

  // Iteration over all the properties.
  virtual PropertyIterator begin() const = 0;
  virtual PropertyIterator end() const = 0;
};

}  // namespace grpc

#endif  // GRPCXX_AUTH_CONTEXT_H

