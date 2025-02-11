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

#include <grpc/event_engine/memory_allocator.h>
#include <stddef.h>

#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "fuzztest/fuzztest.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/chunked_vector.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/util/chunked_vector_fuzzer.pb.h"

bool squelch = true;
bool leak_check = true;

static constexpr size_t kChunkSize = 17;
using IntHdl = std::shared_ptr<int>;

namespace grpc_core {
struct Comparison {
  explicit Comparison(Arena* arena) : chunked(arena) {}

  ChunkedVector<IntHdl, kChunkSize> chunked;
  std::vector<IntHdl> std;

  // Check that both chunked and std are equivalent.
  void AssertOk() const {
    CHECK(std.size() == chunked.size());
    auto it_chunked = chunked.cbegin();
    auto it_std = std.cbegin();
    while (it_std != std.cend()) {
      CHECK(**it_std == **it_chunked);
      ++it_chunked;
      ++it_std;
    }
    CHECK(it_chunked == chunked.cend());
  }
};

class Fuzzer {
 public:
  Fuzzer() = default;
  ~Fuzzer() = default;

  void Act(const chunked_vector_fuzzer::Action& action) {
    switch (action.action_type_case()) {
      case chunked_vector_fuzzer::Action::kEmplaceBack: {
        // Add some value to the back of a comparison, assert that both vectors
        // are equivalent.
        auto* c = Mutate(action.emplace_back().vector());
        c->chunked.EmplaceBack(
            std::make_shared<int>(action.emplace_back().value()));
        c->std.emplace_back(
            std::make_shared<int>(action.emplace_back().value()));
        c->AssertOk();
      } break;
      case chunked_vector_fuzzer::Action::kPopBack: {
        // Remove some value to the back of a comparison, assert that both
        // vectors are equivalent.
        auto* c = Mutate(action.pop_back().vector());
        if (!c->chunked.empty()) {
          c->chunked.PopBack();
          c->std.pop_back();
          c->AssertOk();
        }
      } break;
      case chunked_vector_fuzzer::Action::kCopy: {
        // Copy one vector into another, assert both everything stays
        // equivalent.
        auto it_from = vectors_.find(action.copy().from());
        if (it_from == vectors_.end()) {
          it_from =
              vectors_.emplace(action.copy().from(), Comparison(arena_.get()))
                  .first;
        }
        auto it_to = vectors_.find(action.copy().to());
        if (it_to == vectors_.end()) {
          it_to = vectors_.emplace(action.copy().to(), it_from->second).first;
        } else {
          it_to->second = it_from->second;
        }
        it_from->second.AssertOk();
        it_to->second.AssertOk();
      } break;
      case chunked_vector_fuzzer::Action::kMove: {
        // Move one vector into another, assert both everything stays
        // equivalent.
        auto it_from = vectors_.find(action.move().from());
        if (it_from == vectors_.end()) {
          it_from =
              vectors_.emplace(action.move().from(), Comparison(arena_.get()))
                  .first;
        }
        auto it_to = vectors_.find(action.move().to());
        if (it_to == vectors_.end()) {
          it_to =
              vectors_.emplace(action.move().to(), std::move(it_from->second))
                  .first;
        } else {
          it_to->second = it_from->second;
        }
        it_from->second.AssertOk();
        it_to->second.AssertOk();
      } break;
      case chunked_vector_fuzzer::Action::kClear: {
        // Clear a vector, assert that both underlying vectors are equivalent.
        auto* c = Mutate(action.clear().vector());
        c->chunked.Clear();
        c->std.clear();
        c->AssertOk();
      } break;
      case chunked_vector_fuzzer::Action::kSwap: {
        // Swap two vectors, assert that both underlying vectors are equivalent.
        auto* from = Mutate(action.swap().from());
        auto* to = Mutate(action.swap().to());
        from->chunked.Swap(&to->chunked);
        from->std.swap(to->std);
        from->AssertOk();
      } break;
      case chunked_vector_fuzzer::Action::kRemoveIf: {
        // Apply std::remove_if to a vector, assert that underlying vectors
        // remain equivalent.
        auto cond = [&](const IntHdl& hdl) {
          return *hdl == action.remove_if().value();
        };
        auto* c = Mutate(action.remove_if().vector());
        c->chunked.SetEnd(
            std::remove_if(c->chunked.begin(), c->chunked.end(), cond));
        c->std.erase(std::remove_if(c->std.begin(), c->std.end(), cond),
                     c->std.end());
        c->AssertOk();
      } break;
      case chunked_vector_fuzzer::Action::ACTION_TYPE_NOT_SET:
        break;
    }
  }

 private:
  Comparison* Mutate(int index) {
    auto it = vectors_.find(index);
    if (it != vectors_.end()) {
      return &it->second;
    }
    return &vectors_.emplace(index, Comparison(arena_.get())).first->second;
  }

  MemoryAllocator memory_allocator_ = MemoryAllocator(
      ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator("test"));
  RefCountedPtr<Arena> arena_ = SimpleArenaAllocator(128)->MakeArena();
  std::map<int, Comparison> vectors_;
};

void SameAsVector(const chunked_vector_fuzzer::Msg& msg) {
  Fuzzer fuzzer;
  for (int i = 0; i < msg.actions_size(); i++) {
    fuzzer.Act(msg.actions(i));
  }
}
FUZZ_TEST(ChunkedVectorTest, SameAsVector);
}  // namespace grpc_core
