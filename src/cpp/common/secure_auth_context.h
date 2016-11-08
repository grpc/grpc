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

#ifndef GRPC_INTERNAL_CPP_COMMON_SECURE_AUTH_CONTEXT_H
#define GRPC_INTERNAL_CPP_COMMON_SECURE_AUTH_CONTEXT_H

#include <grpc++/security/auth_context.h>

struct grpc_auth_context;

namespace grpc {

class SecureAuthContext final : public AuthContext {
 public:
  SecureAuthContext(grpc_auth_context* ctx, bool take_ownership);

  ~SecureAuthContext() override;

  bool IsPeerAuthenticated() const override;

  std::vector<grpc::string_ref> GetPeerIdentity() const override;

  grpc::string GetPeerIdentityPropertyName() const override;

  std::vector<grpc::string_ref> FindPropertyValues(
      const grpc::string& name) const override;

  AuthPropertyIterator begin() const override;

  AuthPropertyIterator end() const override;

  void AddProperty(const grpc::string& key,
                   const grpc::string_ref& value) override;

  virtual bool SetPeerIdentityPropertyName(const grpc::string& name) override;

 private:
  grpc_auth_context* ctx_;
  bool take_ownership_;
};

}  // namespace grpc

#endif  // GRPC_INTERNAL_CPP_COMMON_SECURE_AUTH_CONTEXT_H
