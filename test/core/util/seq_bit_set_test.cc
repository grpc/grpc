// Copyright 2025 gRPC authors.
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

#include "src/core/util/seq_bit_set.h"

#include <cstdint>
#include <set>
#include <vector>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace grpc_core {

void SameAsSet(const std::vector<uint64_t>& set_seqs,
               std::set<uint64_t> check_seqs) {
  std::set<uint64_t> std_set;
  SeqBitSet seq_bit_set;
  for (auto seq : set_seqs) {
    check_seqs.insert(seq);
    check_seqs.insert(seq - 1);
    check_seqs.insert(seq + 1);
    bool was_set = std_set.count(seq) != 0;
    std_set.insert(seq);
    EXPECT_EQ(seq_bit_set.Set(seq), was_set);
  }
  for (auto seq : check_seqs) {
    bool found = std_set.count(seq) != 0;
    EXPECT_EQ(found, seq_bit_set.IsSet(seq));
  }
}
FUZZ_TEST(SeqBitSetTest, SameAsSet);

}  // namespace grpc_core
