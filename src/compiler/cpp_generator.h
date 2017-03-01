/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_INTERNAL_COMPILER_CPP_GENERATOR_H
#define GRPC_INTERNAL_COMPILER_CPP_GENERATOR_H

// cpp_generator.h/.cc do not directly depend on GRPC/ProtoBuf, such that they
// can be used to generate code for other serialization systems, such as
// FlatBuffers.

#include <memory>
#include <vector>

#include "src/compiler/config.h"

#ifndef GRPC_CUSTOM_STRING
#include <string>
#define GRPC_CUSTOM_STRING std::string
#endif

namespace grpc {

typedef GRPC_CUSTOM_STRING string;

}  // namespace grpc

namespace grpc_cpp_generator {

// Contains all the parameters that are parsed from the command line.
struct Parameters {
  // Puts the service into a namespace
  grpc::string services_namespace;
  // Use system includes (<>) or local includes ("")
  bool use_system_headers;
  // Prefix to any grpc include
  grpc::string grpc_search_path;
};

// A common interface for objects having comments in the source.
// Return formatted comments to be inserted in generated code.
struct CommentHolder {
  virtual ~CommentHolder() {}
  virtual grpc::string GetLeadingComments() const = 0;
  virtual grpc::string GetTrailingComments() const = 0;
};

// An abstract interface representing a method.
struct Method : public CommentHolder {
  virtual ~Method() {}

  virtual grpc::string name() const = 0;

  virtual grpc::string input_type_name() const = 0;
  virtual grpc::string output_type_name() const = 0;

  virtual bool NoStreaming() const = 0;
  virtual bool ClientOnlyStreaming() const = 0;
  virtual bool ServerOnlyStreaming() const = 0;
  virtual bool BidiStreaming() const = 0;
};

// An abstract interface representing a service.
struct Service : public CommentHolder {
  virtual ~Service() {}

  virtual grpc::string name() const = 0;

  virtual int method_count() const = 0;
  virtual std::unique_ptr<const Method> method(int i) const = 0;
};

struct Printer {
  virtual ~Printer() {}

  virtual void Print(const std::map<grpc::string, grpc::string> &vars,
                     const char *template_string) = 0;
  virtual void Print(const char *string) = 0;
  virtual void Indent() = 0;
  virtual void Outdent() = 0;
};

// An interface that allows the source generated to be output using various
// libraries/idls/serializers.
struct File : public CommentHolder {
  virtual ~File() {}

  virtual grpc::string filename() const = 0;
  virtual grpc::string filename_without_ext() const = 0;
  virtual grpc::string message_header_ext() const = 0;
  virtual grpc::string service_header_ext() const = 0;
  virtual grpc::string package() const = 0;
  virtual std::vector<grpc::string> package_parts() const = 0;
  virtual grpc::string additional_headers() const = 0;

  virtual int service_count() const = 0;
  virtual std::unique_ptr<const Service> service(int i) const = 0;

  virtual std::unique_ptr<Printer> CreatePrinter(grpc::string *str) const = 0;
};

// Return the prologue of the generated header file.
grpc::string GetHeaderPrologue(File *file, const Parameters &params);

// Return the includes needed for generated header file.
grpc::string GetHeaderIncludes(File *file, const Parameters &params);

// Return the includes needed for generated source file.
grpc::string GetSourceIncludes(File *file, const Parameters &params);

// Return the epilogue of the generated header file.
grpc::string GetHeaderEpilogue(File *file, const Parameters &params);

// Return the prologue of the generated source file.
grpc::string GetSourcePrologue(File *file, const Parameters &params);

// Return the services for generated header file.
grpc::string GetHeaderServices(File *file, const Parameters &params);

// Return the services for generated source file.
grpc::string GetSourceServices(File *file, const Parameters &params);

// Return the epilogue of the generated source file.
grpc::string GetSourceEpilogue(File *file, const Parameters &params);

}  // namespace grpc_cpp_generator

#endif  // GRPC_INTERNAL_COMPILER_CPP_GENERATOR_H
