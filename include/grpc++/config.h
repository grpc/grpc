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

#ifndef GRPCXX_CONFIG_H
#define GRPCXX_CONFIG_H

#ifdef GRPC_OLD_CXX
#define GRPC_FINAL
#define GRPC_OVERRIDE
#else
#define GRPC_FINAL final
#define GRPC_OVERRIDE override
#endif

#ifndef GRPC_CUSTOM_PROTOBUF_INT64
#include <google/protobuf/stubs/common.h>
#define GRPC_CUSTOM_PROTOBUF_INT64 ::google::protobuf::int64
#endif

#ifndef GRPC_CUSTOM_MESSAGE
#include <google/protobuf/message.h>
#define GRPC_CUSTOM_MESSAGE ::google::protobuf::Message
#endif

#ifndef GRPC_CUSTOM_STRING
#include <string>
#define GRPC_CUSTOM_STRING std::string
#endif

#ifndef GRPC_CUSTOM_ZEROCOPYOUTPUTSTREAM
#include <google/protobuf/io/zero_copy_stream.h>
#define GRPC_CUSTOM_ZEROCOPYOUTPUTSTREAM \
  ::google::protobuf::io::ZeroCopyOutputStream
#define GRPC_CUSTOM_ZEROCOPYINPUTSTREAM \
  ::google::protobuf::io::ZeroCopyInputStream
#endif

#ifdef __GNUC__
#if (__GNUC__ * 100 + __GNUC_MINOR__ < 406)
#define GRPC_NO_NULLPTR
#endif
#endif

#ifdef GRPC_NO_NULLPTR
#include <memory>
const class {
public:
  template <class T> operator T*() const {return static_cast<T *>(0);}
  template <class T> operator std::unique_ptr<T>() const {
    return std::unique_ptr<T>(static_cast<T *>(0));
  }
  operator bool() const {return false;}
private:
  void operator&() const = delete;
} nullptr = {};
#endif

namespace grpc {

typedef GRPC_CUSTOM_STRING string;

namespace protobuf {

typedef GRPC_CUSTOM_MESSAGE Message;
typedef GRPC_CUSTOM_PROTOBUF_INT64 int64;

namespace io {
typedef GRPC_CUSTOM_ZEROCOPYOUTPUTSTREAM ZeroCopyOutputStream;
typedef GRPC_CUSTOM_ZEROCOPYINPUTSTREAM ZeroCopyInputStream;
}  // namespace io

}  // namespace protobuf

}  // namespace grpc

#endif  // GRPCXX_CONFIG_H
