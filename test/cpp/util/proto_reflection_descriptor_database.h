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
#ifndef GRPC_TEST_CPP_PROTO_SERVER_REFLECTION_DATABSE_H
#define GRPC_TEST_CPP_PROTO_SERVER_REFLECTION_DATABSE_H

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <grpc++/grpc++.h>
#include <grpc++/impl/codegen/config_protobuf.h>
#include "src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h"

namespace grpc {

// ProtoReflectionDescriptorDatabase takes a stub of ServerReflection and
// provides the methods defined by DescriptorDatabase interfaces. It can be used
// to feed a DescriptorPool instance.
class ProtoReflectionDescriptorDatabase : public protobuf::DescriptorDatabase {
 public:
  explicit ProtoReflectionDescriptorDatabase(
      std::unique_ptr<reflection::v1alpha::ServerReflection::Stub> stub);

  explicit ProtoReflectionDescriptorDatabase(
      std::shared_ptr<grpc::Channel> channel);

  virtual ~ProtoReflectionDescriptorDatabase();

  // The following four methods implement DescriptorDatabase interfaces.
  //
  // Find a file by file name.  Fills in in *output and returns true if found.
  // Otherwise, returns false, leaving the contents of *output undefined.
  bool FindFileByName(const string& filename,
                      protobuf::FileDescriptorProto* output) override;

  // Find the file that declares the given fully-qualified symbol name.
  // If found, fills in *output and returns true, otherwise returns false
  // and leaves *output undefined.
  bool FindFileContainingSymbol(const string& symbol_name,
                                protobuf::FileDescriptorProto* output) override;

  // Find the file which defines an extension extending the given message type
  // with the given field number.  If found, fills in *output and returns true,
  // otherwise returns false and leaves *output undefined.  containing_type
  // must be a fully-qualified type name.
  bool FindFileContainingExtension(
      const string& containing_type, int field_number,
      protobuf::FileDescriptorProto* output) override;

  // Finds the tag numbers used by all known extensions of
  // extendee_type, and appends them to output in an undefined
  // order. This method is best-effort: it's not guaranteed that the
  // database will find all extensions, and it's not guaranteed that
  // FindFileContainingExtension will return true on all of the found
  // numbers. Returns true if the search was successful, otherwise
  // returns false and leaves output unchanged.
  bool FindAllExtensionNumbers(const string& extendee_type,
                               std::vector<int>* output) override;

  // Provide a list of full names of registered services
  bool GetServices(std::vector<grpc::string>* output);

 private:
  typedef ClientReaderWriter<
      grpc::reflection::v1alpha::ServerReflectionRequest,
      grpc::reflection::v1alpha::ServerReflectionResponse>
      ClientStream;

  const protobuf::FileDescriptorProto ParseFileDescriptorProtoResponse(
      const grpc::string& byte_fd_proto);

  void AddFileFromResponse(
      const grpc::reflection::v1alpha::FileDescriptorResponse& response);

  const std::shared_ptr<ClientStream> GetStream();

  bool DoOneRequest(
      const grpc::reflection::v1alpha::ServerReflectionRequest& request,
      grpc::reflection::v1alpha::ServerReflectionResponse& response);

  std::shared_ptr<ClientStream> stream_;
  grpc::ClientContext ctx_;
  std::unique_ptr<grpc::reflection::v1alpha::ServerReflection::Stub> stub_;
  std::unordered_set<string> known_files_;
  std::unordered_set<string> missing_symbols_;
  std::unordered_map<string, std::unordered_set<int>> missing_extensions_;
  std::unordered_map<string, std::vector<int>> cached_extension_numbers_;
  std::mutex stream_mutex_;

  protobuf::SimpleDescriptorDatabase cached_db_;
};

}  // namespace grpc

#endif  // GRPC_TEST_CPP_METRICS_SERVER_H
