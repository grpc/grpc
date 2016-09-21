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

#include "test/cpp/util/service_describer.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace grpc {
namespace testing {

grpc::string DescribeServiceList(std::vector<grpc::string> service_list,
                                 grpc::protobuf::DescriptorPool& desc_pool) {
  std::stringstream result;
  for (auto it = service_list.begin(); it != service_list.end(); it++) {
    auto const& service = *it;
    const grpc::protobuf::ServiceDescriptor* service_desc =
        desc_pool.FindServiceByName(service);
    if (service_desc != nullptr) {
      result << DescribeService(service_desc);
    }
  }
  return result.str();
}

grpc::string DescribeService(const grpc::protobuf::ServiceDescriptor* service) {
  grpc::string result;
  if (service->options().deprecated()) {
    result.append("DEPRECATED\n");
  }
  result.append("filename: " + service->file()->name() + "\n");

  grpc::string package = service->full_name();
  size_t pos = package.rfind("." + service->name());
  if (pos != grpc::string::npos) {
    package.erase(pos);
    result.append("package: " + package + ";\n");
  }
  result.append("service " + service->name() + " {\n");
  for (int i = 0; i < service->method_count(); ++i) {
    result.append(DescribeMethod(service->method(i)));
  }
  result.append("}\n\n");
  return result;
}

grpc::string DescribeMethod(const grpc::protobuf::MethodDescriptor* method) {
  std::stringstream result;
  result << "  rpc " << method->name()
         << (method->client_streaming() ? "(stream " : "(")
         << method->input_type()->full_name() << ") returns "
         << (method->server_streaming() ? "(stream " : "(")
         << method->output_type()->full_name() << ") {}\n";
  if (method->options().deprecated()) {
    result << " DEPRECATED";
  }
  return result.str();
}

grpc::string SummarizeService(
    const grpc::protobuf::ServiceDescriptor* service) {
  grpc::string result;
  for (int i = 0; i < service->method_count(); ++i) {
    result.append(SummarizeMethod(service->method(i)));
  }
  return result;
}

grpc::string SummarizeMethod(const grpc::protobuf::MethodDescriptor* method) {
  grpc::string result = method->name();
  result.append("\n");
  return result;
}

}  // namespace testing
}  // namespace grpc
