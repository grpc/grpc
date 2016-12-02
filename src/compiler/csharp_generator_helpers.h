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

#ifndef GRPC_INTERNAL_COMPILER_CSHARP_GENERATOR_HELPERS_H
#define GRPC_INTERNAL_COMPILER_CSHARP_GENERATOR_HELPERS_H

#include "src/compiler/config.h"
#include "src/compiler/generator_helpers.h"

namespace grpc_csharp_generator {

inline bool ServicesFilename(const grpc::protobuf::FileDescriptor *file,
                             grpc::string *file_name_or_error) {
  *file_name_or_error =
      grpc_generator::FileNameInUpperCamel(file, false) + "Grpc.cs";
  return true;
}

// Get leading or trailing comments in a string. Comment lines start with "// ".
// Leading detached comments are put in in front of leading comments.
template <typename DescriptorType>
inline grpc::string GetCsharpComments(const DescriptorType *desc,
                                      bool leading) {
  return grpc_generator::GetPrefixedComments(desc, leading, "//");
}

}  // namespace grpc_csharp_generator

#endif  // GRPC_INTERNAL_COMPILER_CSHARP_GENERATOR_HELPERS_H
