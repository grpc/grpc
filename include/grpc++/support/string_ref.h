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

#ifndef GRPCXX_SUPPORT_STRING_REF_H
#define GRPCXX_SUPPORT_STRING_REF_H

#include <iterator>
#include <iosfwd>

#include <grpc++/support/config.h>

namespace grpc {

/// This class is a non owning reference to a string.
///
/// It should be a strict subset of the upcoming std::string_ref.
///
/// \see http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2012/n3442.html
///
/// The constexpr is dropped or replaced with const for legacy compiler
/// compatibility.
class string_ref {
 public:
  // types
  typedef const char* const_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

  // constants
  const static size_t npos;

  // construct/copy.
  string_ref() : data_(nullptr), length_(0) {}
  string_ref(const string_ref& other)
      : data_(other.data_), length_(other.length_) {}
  string_ref& operator=(const string_ref& rhs);
  string_ref(const char* s);
  string_ref(const char* s, size_t l) : data_(s), length_(l) {}
  string_ref(const grpc::string& s) : data_(s.data()), length_(s.length()) {}

  // iterators
  const_iterator begin() const { return data_; }
  const_iterator end() const { return data_ + length_; }
  const_iterator cbegin() const { return data_; }
  const_iterator cend() const { return data_ + length_; }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }
  const_reverse_iterator crbegin() const {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator crend() const {
    return const_reverse_iterator(begin());
  }

  // capacity
  size_t size() const { return length_; }
  size_t length() const { return length_; }
  size_t max_size() const { return length_; }
  bool empty() const { return length_ == 0; }

  // element access
  const char* data() const { return data_; }

  // string operations
  int compare(string_ref x) const;
  bool starts_with(string_ref x) const;
  bool ends_with(string_ref x) const;
  size_t find(string_ref s) const;
  size_t find(char c) const;

  string_ref substr(size_t pos, size_t n = npos) const;

 private:
  const char* data_;
  size_t length_;
};

// Comparison operators
bool operator==(string_ref x, string_ref y);
bool operator!=(string_ref x, string_ref y);
bool operator<(string_ref x, string_ref y);
bool operator>(string_ref x, string_ref y);
bool operator<=(string_ref x, string_ref y);
bool operator>=(string_ref x, string_ref y);

std::ostream& operator<<(std::ostream& stream, const string_ref& string);

}  // namespace grpc

#endif  // GRPCXX_SUPPORT_STRING_REF_H
