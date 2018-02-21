/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPCPP_IMPL_CODEGEN_CONFIG_PROTOBUF_H
#define GRPCPP_IMPL_CODEGEN_CONFIG_PROTOBUF_H

#define GRPC_OPEN_SOURCE_PROTO

#ifndef GRPC_CUSTOM_PROTOBUF_INT64
#include <google/protobuf/stubs/common.h>
#define GRPC_CUSTOM_PROTOBUF_INT64 ::google::protobuf::int64
#endif

#ifndef GRPC_CUSTOM_MESSAGE
#ifdef GRPC_USE_PROTO_LITE
#include <google/protobuf/message_lite.h>
#define GRPC_CUSTOM_MESSAGE ::google::protobuf::MessageLite
#else
#include <google/protobuf/message.h>
#define GRPC_CUSTOM_MESSAGE ::google::protobuf::Message
#endif
#endif

#ifndef GRPC_CUSTOM_DESCRIPTOR
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#define GRPC_CUSTOM_DESCRIPTOR ::google::protobuf::Descriptor
#define GRPC_CUSTOM_DESCRIPTORPOOL ::google::protobuf::DescriptorPool
#define GRPC_CUSTOM_FIELDDESCRIPTOR ::google::protobuf::FieldDescriptor
#define GRPC_CUSTOM_FILEDESCRIPTOR ::google::protobuf::FileDescriptor
#define GRPC_CUSTOM_FILEDESCRIPTORPROTO ::google::protobuf::FileDescriptorProto
#define GRPC_CUSTOM_METHODDESCRIPTOR ::google::protobuf::MethodDescriptor
#define GRPC_CUSTOM_SERVICEDESCRIPTOR ::google::protobuf::ServiceDescriptor
#define GRPC_CUSTOM_SOURCELOCATION ::google::protobuf::SourceLocation
#endif

#ifndef GRPC_CUSTOM_DESCRIPTORDATABASE
#include <google/protobuf/descriptor_database.h>
#define GRPC_CUSTOM_DESCRIPTORDATABASE ::google::protobuf::DescriptorDatabase
#define GRPC_CUSTOM_SIMPLEDESCRIPTORDATABASE \
  ::google::protobuf::SimpleDescriptorDatabase
#endif

#ifndef GRPC_CUSTOM_ZEROCOPYOUTPUTSTREAM
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream.h>
#define GRPC_CUSTOM_ZEROCOPYOUTPUTSTREAM \
  ::google::protobuf::io::ZeroCopyOutputStream
#define GRPC_CUSTOM_ZEROCOPYINPUTSTREAM \
  ::google::protobuf::io::ZeroCopyInputStream
#define GRPC_CUSTOM_CODEDINPUTSTREAM ::google::protobuf::io::CodedInputStream
#endif

namespace grpc {
namespace protobuf {

typedef GRPC_CUSTOM_MESSAGE Message;
typedef GRPC_CUSTOM_PROTOBUF_INT64 int64;

typedef GRPC_CUSTOM_DESCRIPTOR Descriptor;
typedef GRPC_CUSTOM_DESCRIPTORPOOL DescriptorPool;
typedef GRPC_CUSTOM_DESCRIPTORDATABASE DescriptorDatabase;
typedef GRPC_CUSTOM_FIELDDESCRIPTOR FieldDescriptor;
typedef GRPC_CUSTOM_FILEDESCRIPTOR FileDescriptor;
typedef GRPC_CUSTOM_FILEDESCRIPTORPROTO FileDescriptorProto;
typedef GRPC_CUSTOM_METHODDESCRIPTOR MethodDescriptor;
typedef GRPC_CUSTOM_SERVICEDESCRIPTOR ServiceDescriptor;
typedef GRPC_CUSTOM_SIMPLEDESCRIPTORDATABASE SimpleDescriptorDatabase;
typedef GRPC_CUSTOM_SOURCELOCATION SourceLocation;

namespace io {
typedef GRPC_CUSTOM_ZEROCOPYOUTPUTSTREAM ZeroCopyOutputStream;
typedef GRPC_CUSTOM_ZEROCOPYINPUTSTREAM ZeroCopyInputStream;
typedef GRPC_CUSTOM_CODEDINPUTSTREAM CodedInputStream;
}  // namespace io

}  // namespace protobuf
}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_CONFIG_PROTOBUF_H
