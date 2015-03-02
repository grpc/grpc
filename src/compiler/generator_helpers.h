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

#ifndef GRPC_INTERNAL_COMPILER_GENERATOR_HELPERS_H
#define GRPC_INTERNAL_COMPILER_GENERATOR_HELPERS_H

#include <map>
#include <string>

namespace grpc_generator {

inline bool StripSuffix(std::string *filename, const std::string &suffix) {
  if (filename->length() >= suffix.length()) {
    size_t suffix_pos = filename->length() - suffix.length();
    if (filename->compare(suffix_pos, std::string::npos, suffix) == 0) {
      filename->resize(filename->size() - suffix.size());
      return true;
    }
  }

  return false;
}

inline std::string StripProto(std::string filename) {
  if (!StripSuffix(&filename, ".protodevel")) {
    StripSuffix(&filename, ".proto");
  }
  return filename;
}

inline std::string StringReplace(std::string str, const std::string &from,
                                 const std::string &to) {
  size_t pos = 0;

  for (;;) {
    pos = str.find(from, pos);
    if (pos == std::string::npos) {
      break;
    }
    str.replace(pos, from.length(), to);
    pos += to.length();
  }

  return str;
}

}  // namespace grpc_generator

#endif  // GRPC_INTERNAL_COMPILER_GENERATOR_HELPERS_H
