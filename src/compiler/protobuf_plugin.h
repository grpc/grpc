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

#ifndef GRPC_INTERNAL_COMPILER_PROTOBUF_PLUGIN_H
#define GRPC_INTERNAL_COMPILER_PROTOBUF_PLUGIN_H

#include "src/compiler/config.h"
#include "src/compiler/generator_helpers.h"
#include "src/compiler/schema_interface.h"

inline grpc::string DotsToColons(const grpc::string &name) {
  return grpc_generator::StringReplace(name, ".", "::");
}

inline grpc::string DotsToUnderscores(const grpc::string &name) {
  return grpc_generator::StringReplace(name, ".", "_");
}

inline grpc::string ClassName(const grpc::protobuf::Descriptor *descriptor,
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

// TODO(https://github.com/google/protobuf/issues/888):
// Export `ModuleName` from protobuf's
// `src/google/protobuf/compiler/python/python_generator.cc` file.
grpc::string ModuleName(const grpc::string& filename) {
  grpc::string basename = grpc_generator::StripProto(filename);
  basename = grpc_generator::StringReplace(basename, "-", "_");
  basename = grpc_generator::StringReplace(basename, "/", ".");
  return basename + "_pb2";
}

// TODO(https://github.com/google/protobuf/issues/888):
// Export `ModuleAlias` from protobuf's
// `src/google/protobuf/compiler/python/python_generator.cc` file.
grpc::string ModuleAlias(const grpc::string& filename) {
  grpc::string module_name = ModuleName(filename);
  // We can't have dots in the module name, so we replace each with _dot_.
  // But that could lead to a collision between a.b and a_dot_b, so we also
  // duplicate each underscore.
  module_name = grpc_generator::StringReplace(module_name, "_", "__");
  module_name = grpc_generator::StringReplace(module_name, ".", "_dot_");
  return module_name;
}

bool GetModuleAndMessagePath(const grpc::protobuf::Descriptor* type,
                             const grpc::protobuf::ServiceDescriptor* service,
                             grpc::string* out) {
  const grpc::protobuf::Descriptor* path_elem_type = type;
  std::vector<const grpc::protobuf::Descriptor*> message_path;
  do {
    message_path.push_back(path_elem_type);
    path_elem_type = path_elem_type->containing_type();
  } while (path_elem_type);  // implicit nullptr comparison; don't be explicit
  grpc::string file_name = type->file()->name();
  static const int proto_suffix_length = strlen(".proto");
  if (!(file_name.size() > static_cast<size_t>(proto_suffix_length) &&
        file_name.find_last_of(".proto") == file_name.size() - 1)) {
    return false;
  }
  grpc::string service_file_name = service->file()->name();
  grpc::string module =
      service_file_name == file_name ? "" : ModuleAlias(file_name) + ".";
  grpc::string message_type;
  for (auto path_iter = message_path.rbegin(); path_iter != message_path.rend();
       ++path_iter) {
    message_type += (*path_iter)->name() + ".";
  }
  // no pop_back prior to C++11
  message_type.resize(message_type.size() - 1);
  *out = module + message_type;
  return true;
}

// Get all comments (leading, leading_detached, trailing)
template <typename DescriptorType>
inline std::vector<grpc::string> GetAllCommentsHelper(const DescriptorType *desc) {
  std::vector<grpc::string> comments;
  grpc_generator::GetComment(desc, grpc_generator::COMMENTTYPE_LEADING_DETACHED,
                             &comments);
  grpc_generator::GetComment(desc, grpc_generator::COMMENTTYPE_LEADING,
                             &comments);
  grpc_generator::GetComment(desc, grpc_generator::COMMENTTYPE_TRAILING,
                             &comments);
  return comments;
}

// Get leading or trailing comments in a string.
template <typename DescriptorType>
inline grpc::string GetCommentsHelper(const DescriptorType *desc, bool leading, 
                                       const grpc::string &prefix) {
  return grpc_generator::GetPrefixedComments(desc, leading, prefix);
}

class ProtoBufMethod : public Method {
  public:
   ProtoBufMethod(const grpc::protobuf::MethodDescriptor *method)
       : method_(method) {}

   grpc::string name() const { return method_->name(); }

   grpc::string input_type_name() const {
     return ClassName(method_->input_type(), true);
   }
   grpc::string output_type_name() const {
     return ClassName(method_->output_type(), true);
   }

   bool get_module_message_path_input(grpc::string* out) const {
   	 return GetModuleAndMessagePath(method_->input_type(), service,
   									 out);
   }

   bool get_module_message_path_output(grpc::string* out) const {
   	 return GetModuleAndMessagePath(method_->output_type(), service,
   									 out);
   }

   bool NoStreaming() const {
     return !method_->client_streaming() && !method_->server_streaming();
   }

   bool ClientOnlyStreaming() const {
     return method_->client_streaming() && !method_->server_streaming();
   }

   bool ClientStreaming() const {
   	 return method_->client_streaming();
   }

   bool ServerOnlyStreaming() const {
     return !method_->client_streaming() && method_->server_streaming();
   }

   bool ServerStreaming() const {
   	 return method_->server_streaming();
   }

   bool BidiStreaming() const {
     return method_->client_streaming() && method_->server_streaming();
   }

   grpc::string GetComments(bool leading, 
                          const grpc::string prefix) const {
     return GetCommentsHelper(method_, leading, prefix);
   }

   std::vector<grpc::string> GetAllComments() const {
   	 return GetAllCommentsHelper(method_);
   }

  private:
   const grpc::protobuf::MethodDescriptor *method_;
   const grpc::protobuf::ServiceDescriptor *service;
 };

class ProtoBufService : public Service {
  public:
   ProtoBufService(const grpc::protobuf::ServiceDescriptor *service)
       : service_(service) {}

   grpc::string name() const { return service_->name(); }

   int method_count() const { return service_->method_count(); };
   std::unique_ptr<const Method> method(int i) const {
     return std::unique_ptr<const Method>(
         new ProtoBufMethod(service_->method(i)));
   };

   grpc::string GetComments(bool leading, 
                             const grpc::string prefix) const {
     return GetCommentsHelper(service_, leading, prefix);
   }

   std::vector<grpc::string> GetAllComments() const {
     return GetAllCommentsHelper(service_);
   }

  private:
   const grpc::protobuf::ServiceDescriptor *service_;
};

class ProtoBufPrinter : public Printer {
  public:
    ProtoBufPrinter(grpc::string *str)
       : output_stream_(str), printer_(&output_stream_, '$') {}

    void Print(const std::map<grpc::string, grpc::string> &vars,
              const char *string_template) {
      printer_.Print(vars, string_template);
    }

    void Print(const char *string, const char *method,
                 grpc::string name) {
      printer_.Print(string, method, name);
    }

	void Print(const char *string, const char *method,
                  grpc::string name, const char *arg,
                  grpc::string arg_name) {
      printer_.Print(string, method, name, arg, arg_name);
    }

	void Print(const char *string, const char *package_name,
                  const grpc::string service_name, const char *method_name,
                  const grpc::string arg1, const char *type, 
                  const grpc::string arg2) {
      printer_.Print(string, package_name, service_name,
                     method_name, arg1, type, arg2);
    }

    void Print(const char *string) { printer_.Print(string); }
    void Indent() { printer_.Indent(); }
    void Outdent() { printer_.Outdent(); }

  private:
    grpc::protobuf::io::StringOutputStream output_stream_;
    grpc::protobuf::io::Printer printer_;
 };

class ProtoBufFile : public File {
  public:
   ProtoBufFile(const grpc::protobuf::FileDescriptor *file) : file_(file) {}

   grpc::string filename() const { return file_->name(); }
   grpc::string filename_without_ext() const {
     return grpc_generator::StripProto(filename());
   }

   grpc::string cpp_message_header_ext() const { return ".pb.h"; }
   grpc::string cpp_service_header_ext() const { return ".grpc.pb.h"; }

   grpc::string package() const { return file_->package(); }
   std::vector<grpc::string> package_parts() const {
     return grpc_generator::tokenize(package(), ".");
   }

   grpc::string additional_headers() const { return ""; }

   int service_count() const { return file_->service_count(); };
   std::unique_ptr<const Service> service(int i) const {
     return std::unique_ptr<const Service>(
         new ProtoBufService(file_->service(i)));
   }

   std::unique_ptr<Printer> CreatePrinter(
       grpc::string *str) const {
     return std::unique_ptr<Printer>(
         new ProtoBufPrinter(str));
   }

   grpc::string GetComments(bool leading, 
                           const grpc::string prefix) const {
     return GetCommentsHelper(file_, leading, prefix);
   }

   std::vector<grpc::string> GetAllComments() const {
     return GetAllCommentsHelper(file_);
   }

  private:
   const grpc::protobuf::FileDescriptor *file_;
};

#endif  // GRPC_INTERNAL_COMPILER_PROTOBUF_PLUGIN_H
