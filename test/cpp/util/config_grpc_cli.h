/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_TEST_CPP_UTIL_CONFIG_GRPC_CLI_H
#define GRPC_TEST_CPP_UTIL_CONFIG_GRPC_CLI_H

#include <grpc++/impl/codegen/config_protobuf.h>

#ifndef GRPC_CUSTOM_DYNAMICMESSAGEFACTORY
#include <google/protobuf/dynamic_message.h>
#define GRPC_CUSTOM_DYNAMICMESSAGEFACTORY \
  ::google::protobuf::DynamicMessageFactory
#endif

#ifndef GRPC_CUSTOM_DESCRIPTORPOOLDATABASE
#include <google/protobuf/descriptor.h>
#define GRPC_CUSTOM_DESCRIPTORPOOLDATABASE \
  ::google::protobuf::DescriptorPoolDatabase
#define GRPC_CUSTOM_MERGEDDESCRIPTORDATABASE \
  ::google::protobuf::MergedDescriptorDatabase
#endif

#ifndef GRPC_CUSTOM_TEXTFORMAT
#include <google/protobuf/text_format.h>
#define GRPC_CUSTOM_TEXTFORMAT ::google::protobuf::TextFormat
#endif

#ifndef GRPC_CUSTOM_DISKSOURCETREE
#include <google/protobuf/compiler/importer.h>
#define GRPC_CUSTOM_DISKSOURCETREE ::google::protobuf::compiler::DiskSourceTree
#define GRPC_CUSTOM_IMPORTER ::google::protobuf::compiler::Importer
#define GRPC_CUSTOM_MULTIFILEERRORCOLLECTOR \
  ::google::protobuf::compiler::MultiFileErrorCollector
#endif

namespace grpc {
namespace protobuf {

typedef GRPC_CUSTOM_DYNAMICMESSAGEFACTORY DynamicMessageFactory;

typedef GRPC_CUSTOM_DESCRIPTORPOOLDATABASE DescriptorPoolDatabase;
typedef GRPC_CUSTOM_MERGEDDESCRIPTORDATABASE MergedDescriptorDatabase;

typedef GRPC_CUSTOM_TEXTFORMAT TextFormat;

namespace compiler {
typedef GRPC_CUSTOM_DISKSOURCETREE DiskSourceTree;
typedef GRPC_CUSTOM_IMPORTER Importer;
typedef GRPC_CUSTOM_MULTIFILEERRORCOLLECTOR MultiFileErrorCollector;
}  // namespace compiler

}  // namespace protobuf
}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_CONFIG_GRPC_CLI_H
