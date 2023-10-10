/*
 * Copyright (c) 2009-2021, Google LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Google LLC nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Google LLC BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef UPB_PROTOS_GENERATOR_NAMES_H_
#define UPB_PROTOS_GENERATOR_NAMES_H_

#include <string>

#include "google/protobuf/descriptor.pb.h"
#include "protos_generator/output.h"

namespace protos_generator {

namespace protobuf = ::google::protobuf;

inline constexpr absl::string_view kNoPackageNamePrefix = "protos_";

std::string ClassName(const protobuf::Descriptor* descriptor);
std::string QualifiedClassName(const protobuf::Descriptor* descriptor);
std::string QualifiedInternalClassName(const protobuf::Descriptor* descriptor);

std::string CppSourceFilename(const google::protobuf::FileDescriptor* file);
std::string ForwardingHeaderFilename(const google::protobuf::FileDescriptor* file);
std::string UpbCFilename(const google::protobuf::FileDescriptor* file);
std::string CppHeaderFilename(const google::protobuf::FileDescriptor* file);

void WriteStartNamespace(const protobuf::FileDescriptor* file, Output& output);
void WriteEndNamespace(const protobuf::FileDescriptor* file, Output& output);

std::string CppConstType(const protobuf::FieldDescriptor* field);
std::string CppTypeParameterName(const protobuf::FieldDescriptor* field);

std::string MessageBaseType(const protobuf::FieldDescriptor* field,
                            bool is_const);
// Generate protos::Ptr<const Model> to be used in accessors as public
// signatures.
std::string MessagePtrConstType(const protobuf::FieldDescriptor* field,
                                bool is_const);
std::string MessageCProxyType(const protobuf::FieldDescriptor* field,
                              bool is_const);
std::string MessageProxyType(const protobuf::FieldDescriptor* field,
                             bool is_const);

}  // namespace protos_generator

#endif  // UPB_PROTOS_GENERATOR_NAMES_H_
