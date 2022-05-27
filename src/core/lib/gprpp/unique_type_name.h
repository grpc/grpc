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

#ifndef GRPC_CORE_LIB_GPRPP_UNIQUE_TYPE_NAME_H
#define GRPC_CORE_LIB_GPRPP_UNIQUE_TYPE_NAME_H

#include <grpc/support/port_platform.h>

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
/*
// Interface has a virtual method that returns a UniqueTypeName.
class Interface {
 public:
  virtual ~Interface() = default;
  virtual UniqueTypeName type() const = 0;
};

// Implementation uses a static factory instance to return the same
// UniqueTypeName for every instance.
class FooImplementation : public Interface {
 public:
  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("Foo");
    return kFactory.Create();
  }
};
*/

namespace grpc_core {

class UniqueTypeName {
 public:
  // Factory class.  There should be a single static instance of this
  // for each unique type name.
  class Factory {
   public:
    explicit Factory(absl::string_view name) : name_(new std::string(name)) {}

    Factory(const Factory&) = delete;
    Factory& operator=(const Factory&) = delete;

    UniqueTypeName Create() { return UniqueTypeName(*name_); }

   private:
    std::string* name_;
  };

  // Copyable.
  UniqueTypeName(const UniqueTypeName& other) : name_(other.name_) {}
  UniqueTypeName& operator=(const UniqueTypeName& other) {
    name_ = other.name_;
    return *this;
  }

  bool operator==(const UniqueTypeName& other) const {
    return name_.data() == other.name_.data();
  }
  bool operator!=(const UniqueTypeName& other) const {
    return name_.data() != other.name_.data();
  }
  bool operator<(const UniqueTypeName& other) const {
    return name_.data() < other.name_.data();
  }

  int Compare(const UniqueTypeName& other) const {
    return QsortCompare(name_.data(), other.name_.data());
  }

  absl::string_view name() const { return name_; }

 private:
  explicit UniqueTypeName(absl::string_view name) : name_(name) {}

  absl::string_view name_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_GPRPP_UNIQUE_TYPE_NAME_H
