/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_SRC_CPP_SERVER_LOAD_REPORTER_UTIL_H
#define GRPC_SRC_CPP_SERVER_LOAD_REPORTER_UTIL_H

#include <set>
#include <unordered_map>
#include <vector>

namespace grpc {
namespace load_reporter {

const grpc::string kInvalidLbId = "<INVALID_LBID_238dsb234890rb>";
const uint8_t kLbIdLen = 8;

template <typename K, typename V>
bool UnorderedMapOfSetEraseKeyValue(std::unordered_map<K, std::set<V>>& map,
                                    const K& key, const V& value) {
  auto it = map.find(key);
  if (it != map.end()) {
    auto erased = it->second.erase(value);
    if (it->second.size() == 0) {
      map.erase(key);
    }
    return erased;
  }
  return false;
};

template <typename K, typename V>
std::set<V> UnorderedMapOfSetExtract(std::unordered_map<K, std::set<V>>& map,
                                     const K& key) {
  auto it = map.find(key);
  if (it != map.end()) {
    auto set = map.find(key)->second;
    map.erase(key);
    return set;
  }
  return {};
};

}  // namespace load_reporter
}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_LOAD_REPORTER_UTIL_H
