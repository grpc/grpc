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

#ifndef GRPC_C_DECLARE_SERIALIZER_H
#define GRPC_C_DECLARE_SERIALIZER_H

/**
 * Procedures to hook up gRPC-C to a user-defined serialization mechanism
 * ======================================================================
 *
 * First take a look at
 * https://github.com/google/flatbuffers/blob/48f37f9e0a04f2b60046dda7fef20a8b0ebc1a70/include/flatbuffers/grpc.h
 * which glues FlatBuffers to gRPC-C++. For every new serialization algorithm,
 * we need to create a similar file that imports this declare_serializer.h, and
 * partially specializes the GRPC_SERIALIZATION_IMPL_MSGTYPE macro defined here.
 * This mirrors the C++ template partial specialization method and allows
 * plugging in new serialization implementations with zero knowledge from the
 * gRPC library. Of course we need to include this file in our message header,
 * which is in turn referenced by the generated service implementation. This
 * will typically be controlled by a switch in the codegen, so as to avoid
 * constantly pulling in the gRPC dependency in any other use cases of the
 * serialization library.
 *
 * Because we wouldn't want to hack the Nanopb, specializations for Nanopb are
 * hardcoded in the gRPC library, and are automatically activated when Nanopb
 * objects are detected.
 *
 * The service implementation expands the GRPC_C_RESOLVE_SERIALIZER(MessageType)
 * macro, which is expected to provide the grpc_serialization_impl struct
 * instance that handles serialization for that particular message type.
 */

#define GRPC_C_RESOLVE_SERIALIZER(msgType) \
  GRPC_C_FETCH_SERIALIZER(GRPC_C_DECLARE_SERIALIZATION_##msgType)
#define GRPC_C_RESOLVE_DESERIALIZER(msgType) \
  GRPC_C_FETCH_DESERIALIZER(GRPC_C_DECLARE_SERIALIZATION_##msgType)
#define GRPC_C_FETCH_SERIALIZER(...) \
  GRPC_C_FETCH_SERIALIZER_PRIMITIVE(__VA_ARGS__)
#define GRPC_C_FETCH_DESERIALIZER(...) \
  GRPC_C_FETCH_DESERIALIZER_PRIMITIVE(__VA_ARGS__)
#define GRPC_C_FETCH_SERIALIZER_PRIMITIVE(x, y) x
#define GRPC_C_FETCH_DESERIALIZER_PRIMITIVE(x, y) y

/**
 * Syntax: write this before including gRPC service headers.
 *
 * #define GRPC_C_DECLARE_SERIALIZATION_Foo foo_serialize, foo_deserialize
 *
 * This will cause gRPC-C to invoke foo_serialize when sending Foo, and
 * correspondingly foo_deserialize when receiving Foo.
 */

#endif /* GRPC_C_DECLARE_SERIALIZER_H */
