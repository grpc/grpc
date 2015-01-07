/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef NET_GRPC_COMPILER_RUBY_GENERATOR_STRING_INL_H_
#define NET_GRPC_COMPILER_RUBY_GENERATOR_STRING_INL_H_

#include <algorithm>
#include <string>
#include <sstream>
#include <vector>

using std::getline;
using std::transform;

namespace grpc_ruby_generator {

// Split splits a string using char into elems.
inline vector<string> &Split(const string &s, char delim,
                             vector<string>* elems) {
  stringstream ss(s);
  string item;
  while (getline(ss, item, delim)) {
    elems->push_back(item);
  }
  return *elems;
}

// Split splits a string using char, returning the result in a vector.
inline vector<string> Split(const string &s, char delim) {
  vector<string> elems;
  Split(s, delim, &elems);
  return elems;
}

// Replace replaces from with to in s.
inline string Replace(string s, const string& from, const string& to) {
  size_t start_pos = s.find(from);
  if (start_pos == string::npos) {
    return s;
  }
  s.replace(start_pos, from.length(), to);
  return s;
}

// ReplaceAll replaces all instances of search with replace in s.
inline string ReplaceAll(string s, const string& search,
                         const string& replace) {
  size_t pos = 0;
  while ((pos = s.find(search, pos)) != string::npos) {
    s.replace(pos, search.length(), replace);
    pos += replace.length();
  }
  return s;
}

// ReplacePrefix replaces from with to in s if search is a prefix of s.
inline bool ReplacePrefix(string* s, const string& from, const string& to) {
  size_t start_pos = s->find(from);
  if (start_pos == string::npos || start_pos != 0) {
    return false;
  }
  s->replace(start_pos, from.length(), to);
  return true;
}

// CapitalizeFirst capitalizes the first char in a string.
inline string CapitalizeFirst(string s) {
  if (s.empty()) {
    return s;
  }
  s[0] = ::toupper(s[0]);
  return s;
}

// RubyTypeOf updates a proto type to the required ruby equivalent.
inline string RubyTypeOf(const string& a_type, const string& package) {
  string res(a_type);
  ReplacePrefix(&res, package, "");  // remove the leading package if present
  ReplacePrefix(&res, ".", "");  // remove the leading . (no package)
  if (res.find('.') == string::npos) {
    return res;
  } else {
    vector<string> prefixes_and_type = Split(res, '.');
    for (unsigned int i = 0; i < prefixes_and_type.size(); ++i) {
      if (i != 0) {
        res += "::";  // switch '.' to the ruby module delim
      }
      if (i < prefixes_and_type.size() - 1) {
        res += CapitalizeFirst(prefixes_and_type[i]);  // capitalize pkgs
      } else {
        res += prefixes_and_type[i];
      }
    }
    return res;
  }
}

}  // namespace grpc_ruby_generator

#endif  // NET_GRPC_COMPILER_RUBY_GENERATOR_STRING_INL_H_
