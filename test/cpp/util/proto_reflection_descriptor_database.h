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

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/descriptor_database.h>
#include <grpc++/grpc++.h>
#include <grpc++/impl/reflection.grpc.pb.h>

// #include "reflection.grpc.pb.h"

namespace grpc {

class ProtoReflectionDescriptorDatabase
    : public google::protobuf::DescriptorDatabase {
 public:
  explicit ProtoReflectionDescriptorDatabase(
      std::unique_ptr<reflection::v1alpha::ServerReflection::Stub> stub);

  explicit ProtoReflectionDescriptorDatabase(
      std::shared_ptr<grpc::Channel> channel);

  virtual ~ProtoReflectionDescriptorDatabase();

  // DescriptorDatabase methods
  bool FindFileByName(const string& filename,
                      google::protobuf::FileDescriptorProto* output)
      GRPC_OVERRIDE;

  bool FindFileContainingSymbol(const string& symbol_name,
                                google::protobuf::FileDescriptorProto* output)
      GRPC_OVERRIDE;

  bool FindFileContainingExtension(
      const string& containing_type, int field_number,
      google::protobuf::FileDescriptorProto* output) GRPC_OVERRIDE;

  bool FindAllExtensionNumbers(const string& extendee_type,
                               std::vector<int>* output) GRPC_OVERRIDE;

  bool GetServices(std::vector<std::string>* output);

  grpc::reflection::v1alpha::ServerReflection::Stub* stub() {
    return stub_.get();
  }

 private:
  const google::protobuf::FileDescriptorProto ParseFileDescriptorProtoResponse(
      reflection::v1alpha::FileDescriptorProtoResponse* response);

  std::unique_ptr<grpc::reflection::v1alpha::ServerReflection::Stub> stub_;
  std::unordered_set<string> known_files_;
  std::unordered_set<string> missing_symbols_;
  std::unordered_map<string, std::unordered_set<int>> missing_extensions_;
  std::unordered_map<string, std::vector<int>> cached_extension_numbers_;

  google::protobuf::SimpleDescriptorDatabase cached_db_;
};

}  // namespace grpc
