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

// Generates a Python gRPC service interface out of Protobuf IDL.

#include "src/compiler/config.h"
#include "src/compiler/python_generator.h"

 class ProtoBufMethod : public grpc_python_generator::Method {
  public:
   ProtoBufMethod(const grpc::protobuf::MethodDescriptor *method)
     : method_(method) {}

   grpc::string name() const { return method_->name(); }

   grpc::string input_type_name() const {
     return grpc_python_generator::ClassName(method_->input_type(), true);
   }
   grpc::string output_type_name() const {
     return grpc_python_generator::ClassName(method_->output_type(), true);
   }

  private:
   const grpc::protobuf::MethodDescriptor *method_;
 };

 class ProtoBufService : public grpc_python_generator::Service {
  public:
   ProtoBufService(const grpc::protobuf::ServiceDescriptor *service)
     : service_(service) {}

   grpc::string name() const { return service_->name(); }

   int method_count() const { return service_->method_count(); };
   std::unique_ptr<const grpc_python_generator::Method> method(int i) const {
     return std::unique_ptr<const grpc_python_generator::Method>(
           new ProtoBufMethod(service_->method(i)));
   };

  private:
   const grpc::protobuf::ServiceDescriptor *service_;
 };

 class ProtoBufPrinter : public grpc_python_generator::Printer {
  public:
   ProtoBufPrinter(grpc::string *str)
     : output_stream_(str), printer_(&output_stream_, '$') {}

   void Print(const std::map<grpc::string, grpc::string> &vars,
              const char *string_template) {
     printer_.Print(vars, string_template);
   }

   void Print(const char *string) { printer_.Print(string); }
   void Indent() { printer_.Indent(); }
   void Outdent() { printer_.Outdent(); }

  private:
   grpc::protobuf::io::StringOutputStream output_stream_;
   grpc::protobuf::io::Printer printer_;
 };

 class ProtoBufFile : public grpc_python_generator::File {
  public:
   ProtoBufFile(const grpc::protobuf::FileDescriptor *file) : file_(file) {}

   grpc::string filename() const { return file_->name(); }
   grpc::string filename_without_ext() const {
     return grpc_generator::StripProto(filename());
   }

   grpc::string header_ext() const { return "_pb2.py"; }

   grpc::string package() const { return file_->package(); }
   std::vector<grpc::string> package_parts() const {
     return grpc_generator::tokenize(package(), ".");
   }

   grpc::string additional_headers() const { return ""; }

   int service_count() const { return file_->service_count(); };
   std::unique_ptr<const grpc_python_generator::Service> service(int i) const {
     return std::unique_ptr<const grpc_python_generator::Service> (
           new ProtoBufService(file_->service(i)));
   }

   std::unique_ptr<grpc_python_generator::Printer> CreatePrinter(grpc::string *str) const {
     return std::unique_ptr<grpc_python_generator::Printer>(
           new ProtoBufPrinter(str));
   }

  private:
   const grpc::protobuf::FileDescriptor *file_;
 };

 class PythonGrpcGenerator : public grpc::protobuf::compiler::CodeGenerator {
  public:
   PythonGrpcGenerator(const GeneratorConfiguration& config);
   ~PythonGrpcGenerator();

   bool PythonGrpcGenerator::Generate(
       const File* file, const grpc::string& parameter,
       GeneratorContext* context, grpc::string* error) const {
     // Get output file name.
     grpc::string file_name;
     static const int proto_suffix_length = strlen(".proto");
     if (file->name().size() > static_cast<size_t>(proto_suffix_length) &&
         file->name().find_last_of(".proto") == file->name().size() - 1) {
       file_name = file->name().substr(
           0, file->name().size() - proto_suffix_length) + "_pb2.py";
     } else {
       *error = "Invalid proto file name. Proto file must end with .proto";
       return false;
     }

     std::unique_ptr<ZeroCopyOutputStream> output(
         context->OpenForInsert(file_name, "module_scope"));
     CodedOutputStream coded_out(output.get());
     bool success = false;
     grpc::string code = "";
     tie(success, code) = grpc_python_generator::GetServices(file, config_);
      if (success) {
        coded_out.WriteRaw(code.data(), code.size());
        return true;
      } else {
       return false;
      }
    }
  private:
   GeneratorConfiguration config_;
 };

int main(int argc, char* argv[]) {
  grpc_python_generator::GeneratorConfiguration config;
  config.grpc_package_root = "grpc";
  config.beta_package_root = "grpc.beta";
  grpc_python_generator::PythonGrpcGenerator generator(config);
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
