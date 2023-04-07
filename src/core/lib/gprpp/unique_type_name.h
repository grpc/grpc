//
// Copyright 2022 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef GRPC_SRC_CORE_LIB_GPRPP_UNIQUE_TYPE_NAME_H
#define GRPC_SRC_CORE_LIB_GPRPP_UNIQUE_TYPE_NAME_H

#include <grpc/support/port_platform.h>

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"

#include "src/core/lib/gpr/useful.h"

// Provides a type name that is unique by instance rather than by
// string content.  This is useful in cases where there are different
// implementations of a given interface that need to be differentiated from
// each other for down-casting purposes, where it is undesirable to provide
// a registry to avoid name collisions.
//
// Expected usage:
//
//// Interface has a virtual method that returns a UniqueTypeName.
// class Interface {
// public:
// virtual ~Interface() = default;
// virtual UniqueTypeName type() const = 0;
// };

//// Implementation uses a static factory instance to return the same
//// UniqueTypeName for every instance.
// class FooImplementation : public Interface {
// public:
// UniqueTypeName type() const override {
//   static UniqueTypeName::Factory kFactory("Foo");
//   return kFactory.Create();
// }
// };
//

namespace grpc_core {

template <typename T>
class UniqueTypedTypeName {
 public:
  // Factory class.  There should be a single static instance of this
  // for each unique type name.
  class Factory {
   public:
    explicit Factory(absl::string_view name) : name_(new std::string(name)) {}

    Factory(const Factory&) = delete;
    Factory& operator=(const Factory&) = delete;

    UniqueTypedTypeName<T> Create() { return UniqueTypedTypeName<T>(*name_); }

   private:
    std::string* name_;
  };

  // Copyable.
  UniqueTypedTypeName(const UniqueTypedTypeName& other) : name_(other.name_) {}
  UniqueTypedTypeName& operator=(const UniqueTypedTypeName& other) {
    name_ = other.name_;
    return *this;
  }

  bool operator==(const UniqueTypedTypeName& other) const {
    return unique_id() == other.unique_id();
  }
  bool operator!=(const UniqueTypedTypeName& other) const {
    return unique_id() != other.unique_id();
  }
  bool operator<(const UniqueTypedTypeName& other) const {
    return unique_id() < other.unique_id();
  }

  int Compare(const UniqueTypedTypeName& other) const {
    return QsortCompare(unique_id(), other.unique_id());
  }

  absl::string_view name() const { return name_; }

  std::uintptr_t unique_id() const {
    return reinterpret_cast<std::uintptr_t>(name_.data());
  }

 private:
  explicit UniqueTypedTypeName(absl::string_view name) : name_(name) {}

  absl::string_view name_;
};

using UniqueTypeName = UniqueTypedTypeName<absl::string_view>;

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_GPRPP_UNIQUE_TYPE_NAME_H
