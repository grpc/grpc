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

#include <memory>

#include <grpc++/impl/grpc_library.h>

#include <grpc/grpc_security.h>

namespace grpc {

class AuthContext GRPC_FINAL : {
 public:
  typedef std::pair<grpc::string, grpc::string> Property;

  // A peer identity, in general is one or more properties (in which case they
  // have the same name).
  std::vector<grpc::string> GetPeerIdentity() const;
  grpc::string GetPeerIdentityPropertyName() const;

  // Returns all the property values with the given name.
  std::vector<grpc::string> FindPropertyValues(const grpc::string& name) const;

  // Iteration over all the properties.
  std::const_iterator<Property> begin() const;
  std::const_iterator<Property> end() const;

 private:
  grpc_auth_context *ctx_;
};

}  // namespace grpc

#endif  // GRPCXX_AUTH_CONTEXT_H

