//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "test/cpp/util/service_describer.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace grpc {
namespace testing {

std::string DescribeServiceList(std::vector<std::string> service_list,
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

std::string DescribeService(const grpc::protobuf::ServiceDescriptor* service) {
  std::string result;
  if (service->options().deprecated()) {
    result.append("DEPRECATED\n");
  }
  result.append("filename: " + service->file()->name() + "\n");

  std::string package = service->full_name();
  size_t pos = package.rfind("." + service->name());
  if (pos != std::string::npos) {
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

std::string DescribeMethod(const grpc::protobuf::MethodDescriptor* method) {
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

std::string SummarizeService(const grpc::protobuf::ServiceDescriptor* service) {
  std::string result;
  for (int i = 0; i < service->method_count(); ++i) {
    result.append(SummarizeMethod(service->method(i)));
  }
  return result;
}

std::string SummarizeMethod(const grpc::protobuf::MethodDescriptor* method) {
  std::string result = method->name();
  result.append("\n");
  return result;
}

}  // namespace testing
}  // namespace grpc
