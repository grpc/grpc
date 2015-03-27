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

#ifndef GRPC_INTERNAL_COMPILER_RUBY_GENERATOR_MAP_INL_H
#define GRPC_INTERNAL_COMPILER_RUBY_GENERATOR_MAP_INL_H

#include "src/compiler/config.h"

#include <iostream>
#include <initializer_list>
#include <map>
#include <ostream>  // NOLINT
#include <vector>

using std::initializer_list;
using std::map;
using std::vector;

namespace grpc_ruby_generator {

// Converts an initializer list of the form { key0, value0, key1, value1, ... }
// into a map of key* to value*. Is merely a readability helper for later code.
inline std::map<grpc::string, grpc::string> ListToDict(
    const initializer_list<grpc::string> &values) {
  if (values.size() % 2 != 0) {
    std::cerr << "Not every 'key' has a value in `values`."
              << std::endl;
  }
  std::map<grpc::string, grpc::string> value_map;
  auto value_iter = values.begin();
  for (unsigned i = 0; i < values.size() / 2; ++i) {
    grpc::string key = *value_iter;
    ++value_iter;
    grpc::string value = *value_iter;
    value_map[key] = value;
    ++value_iter;
  }
  return value_map;
}

}  // namespace grpc_ruby_generator

#endif  // GRPC_INTERNAL_COMPILER_RUBY_GENERATOR_MAP_INL_H
