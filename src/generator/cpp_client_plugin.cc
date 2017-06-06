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

// Generates cpp gRPC service interface out of Protobuf IDL.
//

#include <memory>
#include <sstream>
#include <cstdlib>

#include "config.h"
#include "cpp_client_generator.h"

// ********************** Helper Functions ***********************//

// These functions were copied from src/compiler/cpp_generator_helpers.cc
// and src/compiler/generator_helpers.cc. Didn't want to depend on files
// from that part of the directory tree.

std::vector<grpc::string> tokenize(const grpc::string &input,
                                    const grpc::string &delimiters) {
  std::vector<grpc::string> tokens;
  size_t pos, last_pos = 0;

  for (;;) {
    bool done = false;
    pos = input.find_first_of(delimiters, last_pos);
    if (pos == grpc::string::npos) {
      done = true;
      pos = input.length();
    }

    tokens.push_back(input.substr(last_pos, pos - last_pos));
    if (done) return tokens;

    last_pos = pos + 1;
  }
}

bool StripSuffix(grpc::string *filename, const grpc::string &suffix) {
  if (filename->length() >= suffix.length()) {
    size_t suffix_pos = filename->length() - suffix.length();
    if (filename->compare(suffix_pos, grpc::string::npos, suffix) == 0) {
      filename->resize(filename->size() - suffix.size());
      return true;
    }
  }

  return false;
}

grpc::string StripProto(grpc::string filename) {
  if (!StripSuffix(&filename, ".protodevel")) {
    StripSuffix(&filename, ".proto");
  }
  return filename;
}

grpc::string StringReplace(grpc::string str, const grpc::string &from,
                                  const grpc::string &to, bool replace_all) {
  size_t pos = 0;

  do {
    pos = str.find(from, pos);
    if (pos == grpc::string::npos) {
      break;
    }
    str.replace(pos, from.length(), to);
    pos += to.length();
  } while (replace_all);

  return str;
}

grpc::string StringReplace(grpc::string str, const grpc::string &from,
                                  const grpc::string &to) {
  return StringReplace(str, from, to, true);
}

grpc::string DotsToColons(const grpc::string &name) {
  return StringReplace(name, ".", "::");
}

grpc::string DotsToUnderscores(const grpc::string &name) {
  return StringReplace(name, ".", "_");
}

grpc::string ClassName(const grpc::protobuf::Descriptor *descriptor,
                              bool qualified) {
  // Find "outer", the descriptor of the top-level message in which
  // "descriptor" is embedded.
  const grpc::protobuf::Descriptor *outer = descriptor;
  while (outer->containing_type() != NULL) outer = outer->containing_type();

  const grpc::string &outer_name = outer->full_name();
  grpc::string inner_name = descriptor->full_name().substr(outer_name.size());

  if (qualified) {
    return "::" + DotsToColons(outer_name) + DotsToUnderscores(inner_name);
  } else {
    return outer->name() + DotsToUnderscores(inner_name);
  }
}


// ********************** Class Declarations  ***********************//

class ProtoBufEnum : public grpc_cpp_client_generator::Enum {
 public:

  ProtoBufEnum(const google::protobuf::EnumDescriptor* enum_in);
  std::string name() const;
  std::string type_name() const;
  std::string random_value_type() const;

  private:
    const google::protobuf::EnumDescriptor* enum_;
};

class ProtoBufField : public grpc_cpp_client_generator::Field {
 public:

  ProtoBufField(const google::protobuf::FieldDescriptor* field);
  Type type() const;
  std::unique_ptr<const grpc_cpp_client_generator::Enum> enum_type() const;
  std::unique_ptr<const grpc_cpp_client_generator::Message> message_type() const;
  grpc::string type_name() const;
  grpc::string name() const;
  bool is_repeated() const;

 private:
  const google::protobuf::FieldDescriptor* field_;
};

class ProtoBufMessage : public grpc_cpp_client_generator::Message {
 public:

  ProtoBufMessage(const google::protobuf::Descriptor* desc);
  grpc::string name() const;
  int field_count() const;
  std::unique_ptr<const grpc_cpp_client_generator::Field> field(int i) const;
  grpc::string type_name() const;

 private:
  const google::protobuf::Descriptor* desc_;

};

class ProtoBufMethod : public grpc_cpp_client_generator::Method {
 public:
  
  ProtoBufMethod(const grpc::protobuf::MethodDescriptor *method);
  grpc::string name() const;

  std::unique_ptr<const grpc_cpp_client_generator::Message> input_message() const;
  std::unique_ptr<const grpc_cpp_client_generator::Message> output_message() const;

  grpc::string input_type_name() const;
  grpc::string output_type_name() const;

  bool NoStreaming() const;
  bool ClientOnlyStreaming() const;
  bool ServerOnlyStreaming() const;
  bool BidiStreaming() const;

 private:
  const grpc::protobuf::MethodDescriptor *method_;
};

class ProtoBufService : public grpc_cpp_client_generator::Service {
 public:

  ProtoBufService(const grpc::protobuf::ServiceDescriptor *service);
  grpc::string name() const;
  int method_count() const;
  std::unique_ptr<const grpc_cpp_client_generator::Method> method(int i) const;

 private:
  const grpc::protobuf::ServiceDescriptor *service_;
};

class ProtoBufPrinter : public grpc_cpp_client_generator::Printer {
 public:
  ProtoBufPrinter(grpc::string *str);

  void Print(const std::map<grpc::string, grpc::string> &vars,
             const char *string_template);

  void Print(const char *string);
  void Indent();
  void Outdent();

 private:
  grpc::protobuf::io::StringOutputStream output_stream_;
  grpc::protobuf::io::Printer printer_;
};

class ProtoBufFile : public grpc_cpp_client_generator::File {
 public:

  ProtoBufFile(const grpc::protobuf::FileDescriptor *file);

  grpc::string filename() const;
  grpc::string filename_without_ext() const;

  grpc::string message_header_ext() const;
  grpc::string service_header_ext() const;

  grpc::string package() const;
  std::vector<grpc::string> package_parts() const;

  grpc::string package_with_colons() const;

  grpc::string additional_headers() const;

  int service_count() const;
  std::unique_ptr<const grpc_cpp_client_generator::Service> service(int i) const;

  std::unique_ptr<grpc_cpp_client_generator::Printer> CreatePrinter(
    grpc::string *str) const;

 private:
  const grpc::protobuf::FileDescriptor *file_;
};

class CppGrpcClientGenerator : public grpc::protobuf::compiler::CodeGenerator {
 public:
  CppGrpcClientGenerator() {}
  virtual ~CppGrpcClientGenerator() {}

  bool Generate(const grpc::protobuf::FileDescriptor *file,
                        const grpc::string &parameter,
                        grpc::protobuf::compiler::GeneratorContext *context,
                        grpc::string *error) const;
};


// ******************* ProtoBufEnum Implementation ***********************//

ProtoBufEnum::ProtoBufEnum(const google::protobuf::EnumDescriptor* enum_in)
    : enum_(enum_in) 
{ }

std::string ProtoBufEnum::name() const { 
  return enum_->name(); 
}

std::string ProtoBufEnum::type_name() const { 
  return DotsToColons(enum_->full_name());
}

std::string ProtoBufEnum::random_value_type() const {
  const google::protobuf::EnumValueDescriptor* val = 
      enum_->FindValueByNumber(rand() % enum_->value_count());
  return DotsToColons(val->full_name());
}


// ******************* ProtoBufField Implementation ***********************//

ProtoBufField::ProtoBufField(const google::protobuf::FieldDescriptor* field)
    : field_(field) 
{ }

ProtoBufField::Type ProtoBufField::type() const {
  return static_cast<Type>(field_->cpp_type());
}

std::unique_ptr<const grpc_cpp_client_generator::Enum> 
ProtoBufField::enum_type() const {
  return std::unique_ptr<const grpc_cpp_client_generator::Enum>(
    new ProtoBufEnum(field_->enum_type()));
}

std::unique_ptr<const grpc_cpp_client_generator::Message> 
ProtoBufField::message_type() const {
  return std::unique_ptr<const grpc_cpp_client_generator::Message>(
    new ProtoBufMessage(field_->message_type()));
}

grpc::string ProtoBufField::type_name() const {
  return grpc::string(field_->type_name());
}

grpc::string ProtoBufField::name() const {
  return field_->name(); 
}

bool ProtoBufField::is_repeated() const {
  return field_->is_repeated();
}


// ******************* ProtoBufMessage Implementation **********************//

ProtoBufMessage::ProtoBufMessage(const google::protobuf::Descriptor* desc) 
    : desc_(desc) 
{ }

grpc::string ProtoBufMessage::name() const {
  return desc_->name(); 
}

int ProtoBufMessage::field_count() const {
  return desc_->field_count(); 
}

std::unique_ptr<const grpc_cpp_client_generator::Field> 
ProtoBufMessage::field(int i) const {
  return std::unique_ptr<const grpc_cpp_client_generator::Field>(
    new ProtoBufField(desc_->field(i)));
}

grpc::string ProtoBufMessage::type_name() const {
  return ClassName(desc_, true);
}


// ******************* ProtoBufMethod Implementation ***********************//

ProtoBufMethod::ProtoBufMethod(const grpc::protobuf::MethodDescriptor *method)
    : method_(method) {}

grpc::string ProtoBufMethod::name() const { 
  return method_->name(); 
}

std::unique_ptr<const grpc_cpp_client_generator::Message> 
ProtoBufMethod::input_message() const {
  return std::unique_ptr<const grpc_cpp_client_generator::Message>(
    new ProtoBufMessage(method_->input_type()));
}

std::unique_ptr<const grpc_cpp_client_generator::Message> 
ProtoBufMethod::output_message() const {
  return std::unique_ptr<const grpc_cpp_client_generator::Message>(
    new ProtoBufMessage(method_->output_type()));
}

grpc::string ProtoBufMethod::input_type_name() const {
  return ClassName(method_->input_type(), true);
}
grpc::string ProtoBufMethod::output_type_name() const {
  return ClassName(method_->output_type(), true);
}

bool ProtoBufMethod::NoStreaming() const {
  return !method_->client_streaming() && !method_->server_streaming();
}

bool ProtoBufMethod::ClientOnlyStreaming() const {
  return method_->client_streaming() && !method_->server_streaming();
}

bool ProtoBufMethod::ServerOnlyStreaming() const {
  return !method_->client_streaming() && method_->server_streaming();
}

bool ProtoBufMethod::BidiStreaming() const {
  return method_->client_streaming() && method_->server_streaming();
}


// ******************* ProtoBufService Implementation ***********************//

ProtoBufService::ProtoBufService(
  const grpc::protobuf::ServiceDescriptor *service)
    : service_(service) 
{ }

grpc::string ProtoBufService::name() const { return service_->name(); }

int ProtoBufService::method_count() const { return service_->method_count(); };

std::unique_ptr<const grpc_cpp_client_generator::Method> 
ProtoBufService::method(int i) const {
  return std::unique_ptr<const grpc_cpp_client_generator::Method>(
      new ProtoBufMethod(service_->method(i)));
};

// ********************* ProtoBufFile Implementation ***********************//

ProtoBufFile::ProtoBufFile(const grpc::protobuf::FileDescriptor *file) 
    : file_(file) 
{ }

grpc::string ProtoBufFile::filename() const { 
  return file_->name(); 
}

grpc::string ProtoBufFile::filename_without_ext() const {
  return StripProto(filename());
}

grpc::string ProtoBufFile::message_header_ext() const { 
  return ".pb.h"; 
}

grpc::string ProtoBufFile::service_header_ext() const { 
  return ".grpc.pb.h";
}

grpc::string ProtoBufFile::package() const { 
  return file_->package(); 
}

std::vector<grpc::string> ProtoBufFile::package_parts() const {
  return tokenize(package(), ".");
}

grpc::string ProtoBufFile::package_with_colons() const { 
  return DotsToColons(file_->package()); 
}

grpc::string ProtoBufFile::additional_headers() const {
 return ""; 
}

int ProtoBufFile::service_count() const { 
  return file_->service_count(); 
}

std::unique_ptr<const grpc_cpp_client_generator::Service> 
ProtoBufFile::service(int i) const {
  return std::unique_ptr<const grpc_cpp_client_generator::Service>(
      new ProtoBufService(file_->service(i)));
}

std::unique_ptr<grpc_cpp_client_generator::Printer> 
ProtoBufFile::CreatePrinter(grpc::string *str) const {
  return std::unique_ptr<grpc_cpp_client_generator::Printer>(
      new ProtoBufPrinter(str));
}


// ************** CppGrpcPrinterGenerator Implementation *******************//

ProtoBufPrinter::ProtoBufPrinter(grpc::string *str)
    : output_stream_(str), printer_(&output_stream_, '$')
{ }

void ProtoBufPrinter::Print(const std::map<grpc::string, grpc::string> &vars,
           const char *string_template) {
  printer_.Print(vars, string_template);
}

void ProtoBufPrinter::Print(const char *string) { 
  printer_.Print(string); 
}

void ProtoBufPrinter::Indent() { 
  printer_.Indent(); 
}

void ProtoBufPrinter::Outdent() {
  printer_.Outdent();  
}

// ************** CppGrpcClientGenerator Implementation *******************//

bool CppGrpcClientGenerator::Generate(
                      const grpc::protobuf::FileDescriptor *file,
                      const grpc::string &parameter,
                      grpc::protobuf::compiler::GeneratorContext *context,
                      grpc::string *error) const {
  if (file->options().cc_generic_services()) {
    *error =
        "cpp grpc proto compiler plugin does not work with generic "
        "services. To generate cpp grpc APIs, please set \""
        "cc_generic_service = false\".";
    return false;
  }

  grpc_cpp_client_generator::Parameters generator_parameters;
  generator_parameters.use_system_headers = true;

  ProtoBufFile pbfile(file);

  if (!parameter.empty()) {
    std::vector<grpc::string> parameters_list =
        tokenize(parameter, ",");
    for (auto parameter_string = parameters_list.begin();
         parameter_string != parameters_list.end(); parameter_string++) {
      std::vector<grpc::string> param =
          tokenize(*parameter_string, "=");
      if (param[0] == "services_namespace") {
        generator_parameters.services_namespace = param[1];
      } else if (param[0] == "use_system_headers") {
        if (param[1] == "true") {
          generator_parameters.use_system_headers = true;
        } else if (param[1] == "false") {
          generator_parameters.use_system_headers = false;
        } else {
          *error = grpc::string("Invalid parameter: ") + *parameter_string;
          return false;
        }
      } else if (param[0] == "grpc_search_path") {
        generator_parameters.grpc_search_path = param[1];
      } else {
        *error = grpc::string("Unknown parameter: ") + *parameter_string;
        return false;
      }
    }
  }

  grpc::string file_name = StripProto(file->name());

  grpc::string header_code =
      grpc_cpp_client_generator::GetClientPrologue(&pbfile, generator_parameters) +
      grpc_cpp_client_generator::GetClientIncludes(&pbfile, generator_parameters) +
      grpc_cpp_client_generator::GetClientServices(&pbfile, generator_parameters) +
      grpc_cpp_client_generator::GetClientEpilogue(&pbfile, generator_parameters);
  std::unique_ptr<grpc::protobuf::io::ZeroCopyOutputStream> client_output(
      context->Open(file_name + ".grpc.client.pb.cc"));
  grpc::protobuf::io::CodedOutputStream client_coded_out(client_output.get());
  client_coded_out.WriteRaw(header_code.data(), header_code.size());

  return true;
}


// *********************** main ***************************//

int main(int argc, char *argv[]) {
  srand(time(NULL));
  CppGrpcClientGenerator generator;
  return grpc::protobuf::compiler::PluginMain(argc, argv, &generator);
}
