/*
 *
 * Copyright 2017, Google Inc.
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

#ifndef TEST_CPP_MICROBENCHMARKS_FULLSTACK_CONTEXT_MUTATORS_H
#define TEST_CPP_MICROBENCHMARKS_FULLSTACK_CONTEXT_MUTATORS_H

#include <grpc++/channel.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/support/log.h>

#include "test/cpp/microbenchmarks/helpers.h"

namespace grpc {
namespace testing {

/*******************************************************************************
 * CONTEXT MUTATORS
 */

static const int kPregenerateKeyCount = 100000;

template <class F>
auto MakeVector(size_t length, F f) -> std::vector<decltype(f())> {
  std::vector<decltype(f())> out;
  out.reserve(length);
  for (size_t i = 0; i < length; i++) {
    out.push_back(f());
  }
  return out;
}

class NoOpMutator {
 public:
  template <class ContextType>
  NoOpMutator(ContextType* context) {}
};

template <int length>
class RandomBinaryMetadata {
 public:
  static const grpc::string& Key() { return kKey; }

  static const grpc::string& Value() {
    return kValues[rand() % kValues.size()];
  }

 private:
  static const grpc::string kKey;
  static const std::vector<grpc::string> kValues;

  static grpc::string GenerateOneString() {
    grpc::string s;
    s.reserve(length + 1);
    for (int i = 0; i < length; i++) {
      s += (char)rand();
    }
    return s;
  }
};

template <int length>
class RandomAsciiMetadata {
 public:
  static const grpc::string& Key() { return kKey; }

  static const grpc::string& Value() {
    return kValues[rand() % kValues.size()];
  }

 private:
  static const grpc::string kKey;
  static const std::vector<grpc::string> kValues;

  static grpc::string GenerateOneString() {
    grpc::string s;
    s.reserve(length + 1);
    for (int i = 0; i < length; i++) {
      s += (char)(rand() % 26 + 'a');
    }
    return s;
  }
};

template <class Generator, int kNumKeys>
class Client_AddMetadata : public NoOpMutator {
 public:
  Client_AddMetadata(ClientContext* context) : NoOpMutator(context) {
    for (int i = 0; i < kNumKeys; i++) {
      context->AddMetadata(Generator::Key(), Generator::Value());
    }
  }
};

template <class Generator, int kNumKeys>
class Server_AddInitialMetadata : public NoOpMutator {
 public:
  Server_AddInitialMetadata(ServerContext* context) : NoOpMutator(context) {
    for (int i = 0; i < kNumKeys; i++) {
      context->AddInitialMetadata(Generator::Key(), Generator::Value());
    }
  }
};

// static initialization

template <int length>
const grpc::string RandomBinaryMetadata<length>::kKey = "foo-bin";

template <int length>
const std::vector<grpc::string> RandomBinaryMetadata<length>::kValues =
    MakeVector(kPregenerateKeyCount, GenerateOneString);

template <int length>
const grpc::string RandomAsciiMetadata<length>::kKey = "foo";

template <int length>
const std::vector<grpc::string> RandomAsciiMetadata<length>::kValues =
    MakeVector(kPregenerateKeyCount, GenerateOneString);

}  // namespace testing
}  // namespace grpc

#endif
