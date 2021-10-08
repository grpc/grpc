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

#include "src/core/lib/avl/avl.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/avl/avl_fuzzer.pb.h"

bool squelch = true;
bool leak_check = true;

namespace grpc_core {

class Fuzzer {
 public:
  void Run(const avl_fuzzer::Msg& msg) {
    CheckEqual();
    for (const auto& action : msg.actions()) {
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
      CheckEqual();
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

}  // namespace grpc_core

DEFINE_PROTO_FUZZER(const avl_fuzzer::Msg& msg) {
  grpc_core::Fuzzer().Run(msg);
}
