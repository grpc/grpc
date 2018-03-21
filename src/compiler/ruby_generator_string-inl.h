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

#ifndef GRPC_INTERNAL_COMPILER_RUBY_GENERATOR_STRING_INL_H
#define GRPC_INTERNAL_COMPILER_RUBY_GENERATOR_STRING_INL_H

#include "src/compiler/config.h"

#include <algorithm>
#include <sstream>
#include <vector>

using std::getline;
using std::transform;

namespace grpc_ruby_generator {

// Split splits a string using char into elems.
inline std::vector<grpc::string>& Split(const grpc::string& s, char delim,
                                        std::vector<grpc::string>* elems) {
  std::stringstream ss(s);
  grpc::string item;
  while (getline(ss, item, delim)) {
    elems->push_back(item);
  }
  return *elems;
}

// Split splits a string using char, returning the result in a vector.
inline std::vector<grpc::string> Split(const grpc::string& s, char delim) {
  std::vector<grpc::string> elems;
  Split(s, delim, &elems);
  return elems;
}

// Replace replaces from with to in s.
inline grpc::string Replace(grpc::string s, const grpc::string& from,
                            const grpc::string& to) {
  size_t start_pos = s.find(from);
  if (start_pos == grpc::string::npos) {
    return s;
  }
  s.replace(start_pos, from.length(), to);
  return s;
}

// ReplaceAll replaces all instances of search with replace in s.
inline grpc::string ReplaceAll(grpc::string s, const grpc::string& search,
                               const grpc::string& replace) {
  size_t pos = 0;
  while ((pos = s.find(search, pos)) != grpc::string::npos) {
    s.replace(pos, search.length(), replace);
    pos += replace.length();
  }
  return s;
}

// ReplacePrefix replaces from with to in s if search is a prefix of s.
inline bool ReplacePrefix(grpc::string* s, const grpc::string& from,
                          const grpc::string& to) {
  size_t start_pos = s->find(from);
  if (start_pos == grpc::string::npos || start_pos != 0) {
    return false;
  }
  s->replace(start_pos, from.length(), to);
  return true;
}

// CapitalizeFirst capitalizes the first char in a string.
inline grpc::string CapitalizeFirst(grpc::string s) {
  if (s.empty()) {
    return s;
  }
  s[0] = ::toupper(s[0]);
  return s;
}

// RubyTypeOf updates a proto type to the required ruby equivalent.
inline grpc::string RubyTypeOf(const grpc::string& a_type,
                               const grpc::string& package) {
  grpc::string res(a_type);
  ReplacePrefix(&res, package, "");  // remove the leading package if present
  ReplacePrefix(&res, ".", "");      // remove the leading . (no package)
  if (res.find('.') == grpc::string::npos) {
    return res;
  } else {
    std::vector<grpc::string> prefixes_and_type = Split(res, '.');
    res.clear();
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

#endif  // GRPC_INTERNAL_COMPILER_RUBY_GENERATOR_STRING_INL_H
