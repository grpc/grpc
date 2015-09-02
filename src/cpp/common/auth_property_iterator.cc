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

#include <grpc++/security/auth_context.h>

#include <grpc/grpc_security.h>

namespace grpc {

AuthPropertyIterator::AuthPropertyIterator()
    : property_(nullptr), ctx_(nullptr), index_(0), name_(nullptr) {}

AuthPropertyIterator::AuthPropertyIterator(
    const grpc_auth_property* property, const grpc_auth_property_iterator* iter)
    : property_(property),
      ctx_(iter->ctx),
      index_(iter->index),
      name_(iter->name) {}

AuthPropertyIterator::~AuthPropertyIterator() {}

AuthPropertyIterator& AuthPropertyIterator::operator++() {
  grpc_auth_property_iterator iter = {ctx_, index_, name_};
  property_ = grpc_auth_property_iterator_next(&iter);
  ctx_ = iter.ctx;
  index_ = iter.index;
  name_ = iter.name;
  return *this;
}

AuthPropertyIterator AuthPropertyIterator::operator++(int) {
  AuthPropertyIterator tmp(*this);
  operator++();
  return tmp;
}

bool AuthPropertyIterator::operator==(const AuthPropertyIterator& rhs) const {
  if (property_ == nullptr || rhs.property_ == nullptr) {
    return property_ == rhs.property_;
  } else {
    return index_ == rhs.index_;
  }
}

bool AuthPropertyIterator::operator!=(const AuthPropertyIterator& rhs) const {
  return !operator==(rhs);
}

const AuthProperty AuthPropertyIterator::operator*() {
  return std::pair<grpc::string_ref, grpc::string_ref>(
      property_->name,
      grpc::string_ref(property_->value, property_->value_length));
}

}  // namespace grpc
