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

const grpc::string kInvalidLbId = "<INVALID_LBID_238dsb234890rb>";
constexpr uint8_t LB_ID_LEN = 8;

template <typename K, typename V>
bool UnorderedMapOfSetEraseKeyValue(std::unordered_map<K, std::set<V>>& map,
                                    const K& key, const V& value) {
  return map.find(key) != map.end() ? map.find(key)->second.erase(value)
                                    : false;
};

template <typename K, typename V>
std::set<K> UnorderedMapOfSetGetKeys(
    const std::unordered_map<K, std::set<V>>& map) {
  std::set<K> keys;
  for (auto it : map) {
    keys.insert(it.first);
  }
  return keys;
};

template <typename K, typename V>
std::set<V> UnorderedMapOfSetFindAll(
    const std::unordered_map<K, std::set<V>>& map, const K& key) {
  return map.find(key) != map.end() ? map.find(key)->second : std::set<V>();
};

template <typename K, typename V>
std::set<V> UnorderedMapOfSetExtract(std::unordered_map<K, std::set<V>>& map,
                                     const K& key) {
  auto values = UnorderedMapOfSetFindAll(map, key);
  map.erase(key);
  return values;
};

#endif  // GRPC_SRC_CPP_SERVER_LOAD_REPORTER_UTIL_H
