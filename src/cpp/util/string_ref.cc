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

#include <grpc++/support/string_ref.h>

#include <string.h>

#include <algorithm>
#include <iostream>

namespace grpc {

const size_t string_ref::npos = size_t(-1);

string_ref& string_ref::operator=(const string_ref& rhs) {
  data_ = rhs.data_;
  length_ = rhs.length_;
  return *this;
}

string_ref::string_ref(const char* s) : data_(s), length_(strlen(s)) {}

string_ref string_ref::substr(size_t pos, size_t n) const {
  if (pos > length_) pos = length_;
  if (n > (length_ - pos)) n = length_ - pos;
  return string_ref(data_ + pos, n);
}

int string_ref::compare(string_ref x) const {
  size_t min_size = length_ < x.length_ ? length_ : x.length_;
  int r = memcmp(data_, x.data_, min_size);
  if (r < 0) return -1;
  if (r > 0) return 1;
  if (length_ < x.length_) return -1;
  if (length_ > x.length_) return 1;
  return 0;
}

bool string_ref::starts_with(string_ref x) const {
  return length_ >= x.length_ && (memcmp(data_, x.data_, x.length_) == 0);
}

bool string_ref::ends_with(string_ref x) const {
  return length_ >= x.length_ &&
         (memcmp(data_ + (length_ - x.length_), x.data_, x.length_) == 0);
}

size_t string_ref::find(string_ref s) const {
  auto it = std::search(cbegin(), cend(), s.cbegin(), s.cend());
  return it == cend() ? npos : std::distance(cbegin(), it);
}

size_t string_ref::find(char c) const {
  auto it = std::find(cbegin(), cend(), c);
  return it == cend() ? npos : std::distance(cbegin(), it);
}

bool operator==(string_ref x, string_ref y) { return x.compare(y) == 0; }

bool operator!=(string_ref x, string_ref y) { return x.compare(y) != 0; }

bool operator<(string_ref x, string_ref y) { return x.compare(y) < 0; }

bool operator<=(string_ref x, string_ref y) { return x.compare(y) <= 0; }

bool operator>(string_ref x, string_ref y) { return x.compare(y) > 0; }

bool operator>=(string_ref x, string_ref y) { return x.compare(y) >= 0; }

std::ostream& operator<<(std::ostream& out, const string_ref& string) {
  return out << grpc::string(string.begin(), string.end());
}

}  // namespace grpc
