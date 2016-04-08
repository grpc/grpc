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

#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <grpc++/grpc++.h>

#include "reflection.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::reflection::v1::GetDescriptorRequest;
using grpc::reflection::v1::GetServiceResponse;
using grpc::reflection::v1::GetMethodResponse;
using grpc::reflection::v1::GetMessageTypeResponse;
using grpc::reflection::v1::ServerReflection;
using grpc::reflection::v1::ListServiceRequest;
using grpc::reflection::v1::ListServiceResponse;
using google::protobuf::ServiceDescriptorProto;
using google::protobuf::FieldDescriptorProto;

class ReflectionClient {
 public:
  ReflectionClient(std::shared_ptr<Channel> channel)
      : stub_(ServerReflection::NewStub(channel)) {}

  void PrintInfo() {
    ListServiceRequest request;
    ListServiceResponse response;
    ClientContext context;
    Status status = stub_->ListService(&context, request, &response);
    if (status.ok()) {
      std::string padding = "";
      std::cout << "Service amount:" << response.services_size() << std::endl;
      for (int i = 0; i < response.services_size(); ++i) {
        if (i != response.services_size() - 1) {
          std::cout << padding << "│ " << std::endl;
          std::cout << padding << "├─" << response.services(i) << std::endl;
          PrintService(response.services(i), padding + "│ ");
        } else {
          std::cout << padding << "│ " << std::endl;
          std::cout << padding << "└─" << response.services(i) << std::endl;
          PrintService(response.services(i), padding + "  ");
        }
      }
    } else {
      std::cout << status.error_message();
    }
  }

  void PrintService(const std::string& service, const std::string padding) {
    GetDescriptorRequest request;
    GetServiceResponse response;
    ClientContext context;
    request.set_name(service);

    Status status = stub_->GetService(&context, request, &response);
    if (status.ok()) {
      std::cout << padding
                << "│ Method amount:" << response.service().method_size()
                << std::endl;
      for (int i = 0; i < response.service().method_size(); ++i) {
        if (i != response.service().method_size() - 1) {
          std::cout << padding << "├─" << response.service().method(i).name()
                    << std::endl;
          PrintMethod(service + '.' + response.service().method(i).name(),
                      padding + "│ ");
        } else {
          std::cout << padding << "└─" << response.service().method(i).name()
                    << std::endl;
          PrintMethod(service + '.' + response.service().method(i).name(),
                      padding + "  ");
        }
      }
    } else {
      std::cout << status.error_message();
    }
  }

  void PrintMethod(const std::string& method, const std::string padding) {
    GetDescriptorRequest request;
    GetMethodResponse response;
    ClientContext context;
    request.set_name(method);

    Status status = stub_->GetMethod(&context, request, &response);
    if (status.ok()) {
      std::string message_type = response.method().input_type();
      message_type.erase(0, 1);
      std::cout << padding << "├─input type: " << message_type << std::endl;
      PrintMessageType(message_type, padding + "│ ");
      message_type = response.method().output_type();
      message_type.erase(0, 1);
      std::cout << padding << "└─output type: " << message_type << std::endl;
      PrintMessageType(message_type, padding + "  ");
    } else {
      std::cout << status.error_message();
    }
  }

  void PrintMessageType(const std::string& type, const std::string padding) {
    GetDescriptorRequest request;
    GetMessageTypeResponse response;
    ClientContext context;
    request.set_name(type);
    Status status = stub_->GetMessageType(&context, request, &response);
    if (status.ok()) {
      // print field
      if (response.message_type().field_size() > 0) {
        std::cout << padding
                  << "│ Field amount:" << response.message_type().field_size()
                  << std::endl;
      }
      for (int i = 0; i < response.message_type().field_size(); ++i) {
        if (i != response.message_type().field_size() - 1) {
          const FieldDescriptorProto field = response.message_type().field(i);
          std::cout << padding << "└─" << field.Label_Name(field.label())
                    << "\t" << field.name() << field.type() << std::endl;
        } else {
          const FieldDescriptorProto field = response.message_type().field(i);
          std::cout << padding << "└─" << std::left << std::setw(20)
                    << field.Label_Name(field.label()) << std::setw(20)
                    << "name: " + field.name() << std::setw(50)
                    << "type: " + (field.has_type_name()
                                       ? field.type_name()
                                       : field.Type_Name(field.type()))
                    << std::endl;
        }
      }
    } else {
      std::cout << status.error_message();
    }
  }

 private:
  std::unique_ptr<ServerReflection::Stub> stub_;
};

int main(int argc, char** argv) {
  int port = 50051;
  if (argc == 2) {
    try {
      port = std::stoi(argv[1]);
      if (port > 65535 || port < 1024) {
        throw std::out_of_range("Port number out of range.");
      }
    } catch (std::invalid_argument&) {
    } catch (std::out_of_range&) {
    }
  }
  ReflectionClient reflection_client(grpc::CreateChannel(
      "localhost:" + std::to_string(port), grpc::InsecureChannelCredentials()));

  reflection_client.PrintInfo();

  return 0;
}
