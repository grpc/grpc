/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef TEST_CPP_MICROBENCHMARKS_FULLSTACK_CONTEXT_MUTATORS_H
#define TEST_CPP_MICROBENCHMARKS_FULLSTACK_CONTEXT_MUTATORS_H

#include <grpc/support/log.h>
#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

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
      s += static_cast<char>(rand());
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
      s += static_cast<char>(rand() % 26 + 'a');
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
