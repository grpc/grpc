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
#include <sstream>
#include <string>
#include <vector>

#include "src/compiler/config.h"

namespace grpc_generator {

inline bool StripSuffix(grpc::string *filename, const grpc::string &suffix) {
  if (filename->length() >= suffix.length()) {
    size_t suffix_pos = filename->length() - suffix.length();
    if (filename->compare(suffix_pos, grpc::string::npos, suffix) == 0) {
      filename->resize(filename->size() - suffix.size());
      return true;
    }
  }

  return false;
}

inline grpc::string StripProto(grpc::string filename) {
  if (!StripSuffix(&filename, ".protodevel")) {
    StripSuffix(&filename, ".proto");
  }
  return filename;
}

inline grpc::string StringReplace(grpc::string str, const grpc::string &from,
                                  const grpc::string &to, bool replace_all) {
  size_t pos = 0;

  do {
    pos = str.find(from, pos);
    if (pos == grpc::string::npos) {
      break;
    }
    str.replace(pos, from.length(), to);
    pos += to.length();
  } while(replace_all);

  return str;
}

inline grpc::string StringReplace(grpc::string str, const grpc::string &from,
                                  const grpc::string &to) {
  return StringReplace(str, from, to, true);
}

inline std::vector<grpc::string> tokenize(const grpc::string &input,
                                          const grpc::string &delimiters) {
  std::vector<grpc::string> tokens;
  size_t pos, last_pos = 0;

  for (;;) {
    bool done = false;
    pos = input.find_first_of(delimiters, last_pos);
    if (pos == grpc::string::npos) {
      done = true;
      pos = input.length();
    }

    tokens.push_back(input.substr(last_pos, pos - last_pos));
    if (done) return tokens;

    last_pos = pos + 1;
  }
}

inline grpc::string CapitalizeFirstLetter(grpc::string s) {
  if (s.empty()) {
    return s;
  }
  s[0] = ::toupper(s[0]);
  return s;
}

inline grpc::string LowercaseFirstLetter(grpc::string s) {
  if (s.empty()) {
    return s;
  }
  s[0] = ::tolower(s[0]);
  return s;
}

inline grpc::string LowerUnderscoreToUpperCamel(grpc::string str) {
  std::vector<grpc::string> tokens = tokenize(str, "_");
  grpc::string result = "";
  for (unsigned int i = 0; i < tokens.size(); i++) {
    result += CapitalizeFirstLetter(tokens[i]);
  }
  return result;
}

inline grpc::string FileNameInUpperCamel(const grpc::protobuf::FileDescriptor *file,
                                         bool include_package_path) {
  std::vector<grpc::string> tokens = tokenize(StripProto(file->name()), "/");
  grpc::string result = "";
  if (include_package_path) {
    for (unsigned int i = 0; i < tokens.size() - 1; i++) {
      result += tokens[i] + "/";
    }
  }
  result += LowerUnderscoreToUpperCamel(tokens.back());
  return result;
}

inline grpc::string FileNameInUpperCamel(const grpc::protobuf::FileDescriptor *file) {
  return FileNameInUpperCamel(file, true);
}

enum MethodType {
  METHODTYPE_NO_STREAMING,
  METHODTYPE_CLIENT_STREAMING,
  METHODTYPE_SERVER_STREAMING,
  METHODTYPE_BIDI_STREAMING
};

inline MethodType GetMethodType(const grpc::protobuf::MethodDescriptor *method) {
  if (method->client_streaming()) {
    if (method->server_streaming()) {
      return METHODTYPE_BIDI_STREAMING;
    } else {
      return METHODTYPE_CLIENT_STREAMING;
    }
  } else {
    if (method->server_streaming()) {
      return METHODTYPE_SERVER_STREAMING;
    } else {
      return METHODTYPE_NO_STREAMING;
    }
  }
}

inline void Split(const grpc::string &s, char delim,
                  std::vector<grpc::string> *append_to) {
  std::istringstream iss(s);
  grpc::string piece;
  while (std::getline(iss, piece)) {
    append_to->push_back(piece);
  }
}

enum CommentType {
  COMMENTTYPE_LEADING,
  COMMENTTYPE_TRAILING,
  COMMENTTYPE_LEADING_DETACHED
};

// Get all the comments and append each line to out.
template <typename DescriptorType>
inline void GetComment(const DescriptorType *desc, CommentType type,
                       std::vector<grpc::string> *out) {
  grpc::protobuf::SourceLocation location;
  if (!desc->GetSourceLocation(&location)) {
    return;
  }
  if (type == COMMENTTYPE_LEADING || type == COMMENTTYPE_TRAILING) {
    const grpc::string &comments = type == COMMENTTYPE_LEADING
                                       ? location.leading_comments
                                       : location.trailing_comments;
    Split(comments, '\n', out);
  } else if (type == COMMENTTYPE_LEADING_DETACHED) {
    for (unsigned int i = 0; i < location.leading_detached_comments.size();
         i++) {
      Split(location.leading_detached_comments[i], '\n', out);
      out->push_back("");
    }
  } else {
    abort();
  }
}

}  // namespace grpc_generator

#endif  // GRPC_INTERNAL_COMPILER_GENERATOR_HELPERS_H
