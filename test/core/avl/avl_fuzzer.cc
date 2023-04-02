// Copyright 2021 gRPC authors.
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

#include <stdlib.h>

#include <algorithm>
#include <map>
#include <utility>

#include "src/core/lib/avl/avl.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/avl/avl_fuzzer.pb.h"

bool squelch = true;
bool leak_check = true;

namespace grpc_core {

class Fuzzer {
 public:
  Fuzzer() { CheckEqual(); }
  ~Fuzzer() { CheckEqual(); }
  void Run(const avl_fuzzer::Action& action) {
    switch (action.action_case()) {
      case avl_fuzzer::Action::kSet:
        avl_ = avl_.Add(action.key(), action.set());
        map_[action.key()] = action.set();
        break;
      case avl_fuzzer::Action::kDel:
        avl_ = avl_.Remove(action.key());
        map_.erase(action.key());
        break;
      case avl_fuzzer::Action::kGet: {
        auto* p = avl_.Lookup(action.key());
        auto it = map_.find(action.key());
        if (it == map_.end() && p != nullptr) abort();
        if (it != map_.end() && p == nullptr) abort();
        if (it != map_.end() && it->second != *p) abort();
      } break;
      case avl_fuzzer::Action::ACTION_NOT_SET:
        break;
    }
  }

 private:
  void CheckEqual() {
    auto it = map_.begin();
    avl_.ForEach([&](int key, int value) {
      if (it == map_.end()) abort();
      if (it->first != key) abort();
      if (it->second != value) abort();
      ++it;
    });
    if (it != map_.end()) abort();
  }

  AVL<int, int> avl_;
  std::map<int, int> map_;
};

template <typename RepeatedField>
AVL<int, int> AvlFromProto(const RepeatedField& p) {
  AVL<int, int> a;
  for (const auto& kv : p) {
    a = a.Add(kv.key(), kv.value());
  }
  return a;
}

template <typename RepeatedField>
std::map<int, int> MapFromProto(const RepeatedField& p) {
  std::map<int, int> a;
  for (const auto& kv : p) {
    a[kv.key()] = kv.value();
  }
  return a;
}

}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const avl_fuzzer::Msg& msg) {
  grpc_core::Fuzzer fuzzer;
  for (const auto& action : msg.actions()) {
    grpc_core::Fuzzer().Run(action);
  }

  for (const auto& cmp : msg.compares()) {
    auto left_avl = grpc_core::AvlFromProto(cmp.left());
    auto left_map = grpc_core::MapFromProto(cmp.left());
    auto right_avl = grpc_core::AvlFromProto(cmp.right());
    auto right_map = grpc_core::MapFromProto(cmp.right());
    if ((left_avl == right_avl) != (left_map == right_map)) abort();
    if ((left_avl < right_avl) != (left_map < right_map)) abort();
  }
}
