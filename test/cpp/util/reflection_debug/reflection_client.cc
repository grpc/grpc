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

#include "proto_reflection_descriptor_database.h"
// #include "reflection.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::ProtoReflectionDescriptorDatabase;
using grpc::reflection::v1alpha::ServerReflection;
using grpc::reflection::v1alpha::EmptyRequest;
using grpc::reflection::v1alpha::ListServiceResponse;
using google::protobuf::FileDescriptorProto;
using google::protobuf::DescriptorPool;
using google::protobuf::ServiceDescriptor;
using google::protobuf::MethodDescriptor;
using google::protobuf::Descriptor;
using google::protobuf::FieldDescriptor;

class ReflectionClient {
 public:
  ReflectionClient(std::shared_ptr<Channel> channel)
      : db_(new ProtoReflectionDescriptorDatabase(
            ServerReflection::NewStub(channel))),
        desc_pool_(new DescriptorPool(db_.get())) {}

  void PrintInfo() {
    EmptyRequest request;
    ListServiceResponse response;
    ClientContext context;
    Status status = db_->stub()->ListService(&context, request, &response);
    if (status.ok()) {
      std::string padding = "";
      std::cout << "Service amount:" << response.services_size() << std::endl;
      for (int i = 0; i < response.services_size(); ++i) {
        if (i != response.services_size() - 1) {
          std::cout << padding << "│ " << std::endl;
          std::cout << padding << "├─" << response.services(i) << std::endl;
          PrintService(desc_pool_->FindServiceByName(response.services(i)),
                       padding + "│ ");
        } else {
          std::cout << padding << "│ " << std::endl;
          std::cout << padding << "└─" << response.services(i) << std::endl;
          PrintService(desc_pool_->FindServiceByName(response.services(i)),
                       padding + "  ");
        }
      }
    } else {
      std::cout << status.error_message();
    }
  }

  void PrintService(const ServiceDescriptor* service_desc,
                    const std::string padding) {
    if (service_desc != nullptr) {
      std::cout << padding << "│ Method amount:" << service_desc->method_count()
                << std::endl;
      for (int i = 0; i < service_desc->method_count(); ++i) {
        if (i != service_desc->method_count() - 1) {
          std::cout << padding << "├─" << service_desc->method(i)->name()
                    << std::endl;
          PrintMethod(service_desc->method(i), padding + "│ ");
        } else {
          std::cout << padding << "└─" << service_desc->method(i)->name()
                    << std::endl;
          PrintMethod(service_desc->method(i), padding + "  ");
        }
      }
    }
  }

  void PrintMethod(const MethodDescriptor* method_desc,
                   const std::string padding) {
    if (method_desc != nullptr) {
      std::cout << padding
                << "├─input type: " << method_desc->input_type()->name()
                << std::endl;
      PrintMessageType(method_desc->input_type(), padding + "│ ");
      std::cout << padding
                << "└─output type: " << method_desc->output_type()->name()
                << std::endl;
      PrintMessageType(method_desc->output_type(), padding + "  ");
    }
  }

  void PrintMessageType(const Descriptor* type_desc,
                        const std::string padding) {
    if (type_desc != nullptr) {
      if (type_desc->field_count() > 0) {
        std::cout << padding << "│ Field amount:" << type_desc->field_count()
                  << std::endl;
      }
      for (int i = 0; i < type_desc->field_count(); ++i) {
        if (i != type_desc->field_count() - 1) {
          const FieldDescriptor* field = type_desc->field(i);
          std::cout << padding << "├─ " << std::left << std::setw(15)
                    << kLabelToName[field->label()] << std::setw(30)
                    << " name: " + field->name() << std::setw(50)
                    << " type: " +
                           (field->type() == FieldDescriptor::Type::TYPE_MESSAGE
                                ? field->message_type()->name()
                                : field->type_name())
                    << std::endl;
        } else {
          const FieldDescriptor* field = type_desc->field(i);
          std::cout << padding << "└─ " << std::left << std::setw(15)
                    << kLabelToName[field->label()] << std::setw(30)
                    << " name: " + field->name() << std::setw(50)
                    << " type: " +
                           (field->type() == FieldDescriptor::Type::TYPE_MESSAGE
                                ? field->message_type()->name()
                                : field->type_name())
                    << std::endl;
        }
      }
    }
  }

  void Test() {
    {
      FileDescriptorProto output;
      bool found = db_->FindFileByName("helloworld.proto", &output);
      if (found) std::cout << output.name() << std::endl;
    }
    {
      FileDescriptorProto output;
      bool found =
          db_->FindFileContainingSymbol("helloworld.Greeter.SayHello", &output);
      if (found) std::cout << output.name() << std::endl;
    }
    {
      FileDescriptorProto output;
      bool found = db_->FindFileContainingExtension(
          "helloworld.Greeter.HelloRequest", 1, &output);
      found = db_->FindFileContainingExtension(
          "helloworld.Greeter.HelloRequest", 1, &output);
      if (found) std::cout << output.name() << std::endl;
    }
    DescriptorPool pool(db_.get());
    std::cout << pool.FindServiceByName("helloworld.Greeter")->name()
              << std::endl;
  }

 private:
  const char* const kLabelToName[FieldDescriptor::Label::MAX_LABEL + 1] = {
      "ERROR",  // 0 is reserved for errors

      "optional",  // LABEL_OPTIONAL
      "required",  // LABEL_REQUIRED
      "repeated",  // LABEL_REPEATED
  };

  std::unique_ptr<ProtoReflectionDescriptorDatabase> db_;
  std::unique_ptr<DescriptorPool> desc_pool_;
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
