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

const grpc::string INVALID_LBID = "<INVALID_LBID_238dsb234890rb>";
constexpr uint8_t LB_ID_LEN = 8;

template <typename K, typename V>
bool UnorderedMultimapEraseKeyValue(std::unordered_multimap<K, V>& map,
                                    const K& key, const V& value) {
  auto range = map.equal_range(key);
  for (auto it = range.first; it != range.second; ++it) {
    if (it->second == value) {
      map.erase(it);
      return true;
    }
  }
  return false;
};

template <typename K, typename V>
std::vector<K> UnorderedMultimapKeys(const std::unordered_multimap<K, V>& map) {
  std::set<K> keys;
  for (auto it : map) {
    keys.insert(it.first);
  }
  return std::vector<K>(keys.begin(), keys.end());
};

template <typename K, typename V>
std::vector<V> UnorderedMultimapFindAll(
    const std::unordered_multimap<K, V>& map, const K& key) {
  std::vector<V> values;
  auto range = map.equal_range(key);
  for (auto it = range.first; it != range.second; ++it) {
    values.push_back(it->second);
  }
  return values;
};

template <typename K, typename V>
std::vector<V> UnorderedMultimapRemoveAll(std::unordered_multimap<K, V>& map,
                                          const K& key) {
  std::vector<V> values = UnorderedMultimapFindAll(map, key);
  map.erase(key);
  return values;
};

#endif  // GRPC_SRC_CPP_SERVER_LOAD_REPORTER_UTIL_H
